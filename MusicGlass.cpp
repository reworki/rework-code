#include "MusicGlass.h"
#include <QEvent>
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QMetaObject>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace {
// The original PRO effect uses two radius-2 box passes. On a large editor we
// process a reduced image with two radius-1 passes, keeping its physical blur
// comparable without processing millions of pixels for every frame.
constexpr int kMaxPixels = 900 * 560;
constexpr auto kFrameInterval = std::chrono::milliseconds(40);
void boxHorizontal(const QImage &src, QImage &dst) {
    const int w=src.width(),h=src.height();
    for(int y=0;y<h;++y){const QRgb *s=reinterpret_cast<const QRgb*>(src.constScanLine(y));QRgb *d=reinterpret_cast<QRgb*>(dst.scanLine(y));int r=0,g=0,b=0;for(int dx=-1;dx<=1;++dx){QRgb c=s[qBound(0,dx,w-1)];r+=qRed(c);g+=qGreen(c);b+=qBlue(c);}for(int x=0;x<w;++x){d[x]=qRgb(r/3,g/3,b/3);QRgb a=s[qMax(0,x-1)],z=s[qMin(w-1,x+2)];r+=qRed(z)-qRed(a);g+=qGreen(z)-qGreen(a);b+=qBlue(z)-qBlue(a);}}
}
void boxVertical(const QImage &src,QImage &dst){
    const int w=src.width(),h=src.height();std::vector<int> r(w),g(w),b(w);
    for(int dy=-1;dy<=1;++dy){const QRgb *s=reinterpret_cast<const QRgb*>(src.constScanLine(qBound(0,dy,h-1)));for(int x=0;x<w;++x){r[x]+=qRed(s[x]);g[x]+=qGreen(s[x]);b[x]+=qBlue(s[x]);}}
    for(int y=0;y<h;++y){QRgb *out=reinterpret_cast<QRgb*>(dst.scanLine(y));const QRgb *a=reinterpret_cast<const QRgb*>(src.constScanLine(qMax(0,y-1))),*z=reinterpret_cast<const QRgb*>(src.constScanLine(qMin(h-1,y+2)));for(int x=0;x<w;++x){out[x]=qRgb(r[x]/3,g[x]/3,b[x]/3);r[x]+=qRed(z[x])-qRed(a[x]);g[x]+=qGreen(z[x])-qGreen(a[x]);b[x]+=qBlue(z[x])-qBlue(a[x]);}}
}
#ifdef Q_OS_WIN
struct Dib {
    HDC screen=nullptr,memory=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;void *pixels=nullptr;int w=0,h=0;
    ~Dib(){if(memory&&old)SelectObject(memory,old);if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);}
    bool prepare(int width,int height){if(!screen)screen=GetDC(nullptr);if(!screen)return false;if(!memory)memory=CreateCompatibleDC(screen);if(!memory)return false;if(bitmap&&w==width&&h==height)return true;if(bitmap){SelectObject(memory,old);DeleteObject(bitmap);bitmap=nullptr;old=nullptr;pixels=nullptr;}BITMAPINFO i{};i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=width;i.bmiHeader.biHeight=-height;i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=32;i.bmiHeader.biCompression=BI_RGB;bitmap=CreateDIBSection(screen,&i,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap||!pixels)return false;old=SelectObject(memory,bitmap);if(!old||old==HGDI_ERROR){DeleteObject(bitmap);bitmap=nullptr;old=nullptr;pixels=nullptr;return false;}SetStretchBltMode(memory,COLORONCOLOR);w=width;h=height;return true;}
};
#endif
}
struct MusicGlass::Impl {
    QPointer<QWidget> window;
    mutable std::mutex requestMutex,frameMutex,captureMutex;
    std::condition_variable wake;
    struct Request {QRect rect; bool visible=false; bool frozen=false;};
    Request request;
    QImage frame; QRect frameRect;
    QString error;
    std::thread worker;
    std::atomic<bool> stopping{false},ready{false},active{false},frozen{false},frameSignalPending{false};
    std::atomic<quint64> generation{0};
    std::atomic<int> suspendCount{0};
    QHash<QWidget*, QPointer<QWidget>> transients;
    QSet<QWidget*> observedTransientLifetimes;
#ifdef Q_OS_WIN
    HWND hwnd=nullptr; DWORD oldAffinity=WDA_NONE; bool affinitySet=false;
#endif
};
MusicGlass::MusicGlass(QWidget *window):QObject(window),d(std::make_unique<Impl>()){d->window=window;if(qApp)qApp->installEventFilter(this);}
MusicGlass::~MusicGlass(){if(qApp)qApp->removeEventFilter(this);stop();}
bool MusicGlass::isActive()const{return d->active.load();}
bool MusicGlass::isReady()const{return d->ready.load();}
bool MusicGlass::isFrozen()const{return d->frozen.load();}
QString MusicGlass::errorString()const{return d->error;}
QImage MusicGlass::latestFrame()const{QRect current;{std::lock_guard<std::mutex> lock(d->requestMutex);current=d->request.rect;}std::lock_guard<std::mutex> lock(d->frameMutex);return (d->frozen.load()||d->suspendCount.load()>0||d->frameRect==current)?d->frame:QImage();}
QImage MusicGlass::filterFrame(const QImage &input){
    if(input.isNull()||input.width()<1||input.height()<1)return {};
    QImage a=input.convertToFormat(QImage::Format_RGB32), b(a.size(),QImage::Format_RGB32);if(a.isNull()||b.isNull())return {};
    for(int pass=0;pass<2;++pass){boxHorizontal(a,b);boxVertical(b,a);}
    const int w=a.width(),h=a.height();QImage out(a.size(),QImage::Format_RGB32);if(out.isNull())return {};
    // PRO's softly refracted rim and subtle chromatic shift, expressed in the
    // reduced-frame coordinate system. Interior stays clear enough for code.
    const float border=qBound(4.0f,float(qMin(w,h))*0.035f,18.0f), shift=qBound(2.0f,border*.55f,9.0f);
    const QRgb *data=reinterpret_cast<const QRgb*>(a.constBits());
    auto at=[&](int x,int y){return data[qBound(0,y,h-1)*w+qBound(0,x,w-1)];};
    for(int y=0;y<h;++y){QRgb *dst=reinterpret_cast<QRgb*>(out.scanLine(y));for(int x=0;x<w;++x){int left=x,right=w-1-x,top=y,bottom=h-1-y,nearest=qMin(qMin(left,right),qMin(top,bottom));float edge=qBound(0.0f,1.0f-float(nearest)/border,1.0f);edge=edge*edge*(3.0f-2.0f*edge);float dx=0,dy=0;if(nearest==left)dx=1;else if(nearest==right)dx=-1;else if(nearest==top)dy=1;else dy=-1;const float s=edge*shift;QRgb r=at(qRound(x+dx*s*1.10f),qRound(y+dy*s*1.10f)),g=at(qRound(x+dx*s),qRound(y+dy*s)),bl=at(qRound(x+dx*s*.90f),qRound(y+dy*s*.90f));dst[x]=qRgb(qRed(r)*97/100,qGreen(g)*97/100,qBlue(bl)*97/100);}}
    return out;
}
void MusicGlass::updateRequest(){
#ifdef Q_OS_WIN
    if(!d->active.load()||!d->hwnd||!d->window)return;RECT rc{};POINT origin{0,0};if(!GetClientRect(d->hwnd,&rc)||!ClientToScreen(d->hwnd,&origin))return;
    QRect rect(origin.x,origin.y,rc.right-rc.left,rc.bottom-rc.top);bool visible=d->window->isVisible()&&!d->window->isMinimized()&&rect.width()>0&&rect.height()>0;
    {std::lock_guard<std::mutex> lock(d->requestMutex);d->request.rect=rect;d->request.visible=visible;d->request.frozen=d->frozen.load()||d->suspendCount.load()>0;}d->wake.notify_all();
#endif
}
void MusicGlass::reportError(const QString &error){if(!d->active.load())return;stop();d->error=error;emit errorOccurred(error);}
bool MusicGlass::start(){
    stop();
#ifdef Q_OS_WIN
    // Never lose the original affinity if restoring it failed: a later stop()
    // can retry, and start must not overwrite the state we still need.
    if(d->affinitySet)return false;
#endif
    d->error.clear();if(!d->window||!d->window->isWindow()){d->error=QStringLiteral("Нет окна для стекла");return false;}
#ifdef Q_OS_WIN
    d->hwnd=reinterpret_cast<HWND>(d->window->winId());if(!d->hwnd||!IsWindow(d->hwnd)){d->error=QStringLiteral("Окно недоступно");return false;}
    DWORD before=WDA_NONE;if(!GetWindowDisplayAffinity(d->hwnd,&before)){d->error=QStringLiteral("Не удалось прочитать настройки захвата окна");return false;}d->oldAffinity=before;
    if(!SetWindowDisplayAffinity(d->hwnd,WDA_EXCLUDEFROMCAPTURE)){d->error=QStringLiteral("Windows не позволяет исключить окно из захвата");return false;}
    // Mark ownership before checking the value: the setter may have succeeded
    // even if reading it back fails, and rollback must remain retryable.
    d->affinitySet=true;
    DWORD observed=0;if(!GetWindowDisplayAffinity(d->hwnd,&observed)||observed!=WDA_EXCLUDEFROMCAPTURE){
        stop();
        if(!d->affinitySet)d->error=QStringLiteral("Исключение окна из захвата недоступно");
        return false;
    }
    d->active=true;d->stopping=false;d->frozen=false;const quint64 gen=++d->generation;updateRequest();
    try{d->worker=std::thread([this,gen]{Dib dib;while(!d->stopping.load()){
        const auto deadline=std::chrono::steady_clock::now()+kFrameInterval;Impl::Request req;{std::lock_guard<std::mutex> lock(d->requestMutex);req=d->request;}
        if(req.visible&&!req.frozen&&req.rect.width()>0&&req.rect.height()>0){
            QImage image;QString failure;
            {std::lock_guard<std::mutex> capture(d->captureMutex);{std::lock_guard<std::mutex> lock(d->requestMutex);req=d->request;}
                if(req.visible&&!req.frozen&&!d->stopping.load()){
                    const int width=req.rect.width(),height=req.rect.height();const double factor=qMin(1.0,std::sqrt(double(kMaxPixels)/(double(width)*height)));const int w=qMax(1,qRound(width*factor)),h=qMax(1,qRound(height*factor));
                    if(!dib.prepare(w,h)||!StretchBlt(dib.memory,0,0,w,h,dib.screen,req.rect.x(),req.rect.y(),width,height,SRCCOPY|CAPTUREBLT)){failure=QStringLiteral("Не удалось получить живой фон");}
                    else {GdiFlush();QImage view(static_cast<const uchar*>(dib.pixels),w,h,w*4,QImage::Format_RGB32);image=view.copy();if(image.isNull())failure=QStringLiteral("Недостаточно памяти для стекла");}
                }
            }
            if(!failure.isEmpty()){QMetaObject::invokeMethod(this,[this,gen,failure]{if(d->active.load()&&d->generation.load()==gen)reportError(failure);},Qt::QueuedConnection);break;}
            if(!image.isNull()){QImage filtered=filterFrame(image);if(filtered.isNull()){QMetaObject::invokeMethod(this,[this,gen]{if(d->active.load()&&d->generation.load()==gen)reportError(QStringLiteral("Ошибка обработки стекла"));},Qt::QueuedConnection);break;}{std::lock_guard<std::mutex> lock(d->frameMutex);d->frame=std::move(filtered);d->frameRect=req.rect;}if(!d->ready.exchange(true))QMetaObject::invokeMethod(this,[this,gen]{if(d->active.load()&&d->generation.load()==gen&&d->ready.load())emit readyChanged(true);},Qt::QueuedConnection);if(!d->frameSignalPending.exchange(true))QMetaObject::invokeMethod(this,[this,gen]{if(d->generation.load()!=gen)return;d->frameSignalPending=false;if(d->active.load())emit frameAvailable();},Qt::QueuedConnection);}
        }
        std::unique_lock<std::mutex> lock(d->requestMutex);d->wake.wait_until(lock,deadline,[this]{return d->stopping.load();});
    }});}catch(...){stop();d->error=QStringLiteral("Не удалось запустить поток стекла");return false;}
    return true;
#else
    d->error=QStringLiteral("Живое стекло доступно только в Windows");return false;
#endif
}
void MusicGlass::stop(){
    bool wasReady=d->ready.exchange(false);d->active=false;d->stopping=true;++d->generation;d->wake.notify_all();if(d->worker.joinable())d->worker.join();
#ifdef Q_OS_WIN
    bool restoreFailed=false;
    if(d->affinitySet){
        if(d->hwnd&&IsWindow(d->hwnd)){
            DWORD observed=0;
            if(!SetWindowDisplayAffinity(d->hwnd,d->oldAffinity)||
               !GetWindowDisplayAffinity(d->hwnd,&observed)||observed!=d->oldAffinity){
                // Retain hwnd, oldAffinity and ownership so a later call can retry.
                restoreFailed=true;
            }else{d->affinitySet=false;d->hwnd=nullptr;}
        }else{
            // A destroyed native window cannot retain capture exclusion.
            d->affinitySet=false;d->hwnd=nullptr;
        }
    }else d->hwnd=nullptr;
    if(restoreFailed){d->error=QStringLiteral("Не удалось восстановить доступность окна для записи или снимка экрана");const QString message=d->error;QTimer::singleShot(0,this,[this,message]{emit errorOccurred(message);});}
#endif
    d->frozen=false;d->suspendCount=0;d->transients.clear();d->frameSignalPending=false;{std::lock_guard<std::mutex> lock(d->frameMutex);d->frame=QImage();d->frameRect=QRect();}{std::lock_guard<std::mutex> lock(d->requestMutex);d->request=Impl::Request();}if(wasReady)emit readyChanged(false);
}
bool MusicGlass::freezeForScreenshot(bool frozen){
#ifdef Q_OS_WIN
    if(!d->active.load()||!d->hwnd)return false;if(d->frozen.load()==frozen)return true;
    if(frozen&&(!d->ready.load()||latestFrame().isNull())){d->error=QStringLiteral("Фон ещё не готов для снимка");return false;}
    {std::lock_guard<std::mutex> lock(d->requestMutex);d->request.frozen=true;}d->wake.notify_all();
    std::lock_guard<std::mutex> capture(d->captureMutex);
    const DWORD affinity=frozen?WDA_NONE:WDA_EXCLUDEFROMCAPTURE;
    if(!SetWindowDisplayAffinity(d->hwnd,affinity)){d->error=QStringLiteral("Не удалось изменить режим снимка");if(!frozen){d->frozen=true;}else{{std::lock_guard<std::mutex> lock(d->requestMutex);d->request.frozen=d->frozen.load()||d->suspendCount.load()>0;}d->wake.notify_all();}return false;}
    DWORD actual=0;if(!GetWindowDisplayAffinity(d->hwnd,&actual)||actual!=affinity){if(!frozen)SetWindowDisplayAffinity(d->hwnd,WDA_NONE);d->error=QStringLiteral("Windows не подтвердила режим снимка");d->frozen=true;return false;}
    d->frozen=frozen;{std::lock_guard<std::mutex> lock(d->requestMutex);d->request.frozen=frozen||d->suspendCount.load()>0;}d->error.clear();d->wake.notify_all();return true;
#else
    Q_UNUSED(frozen);return false;
#endif
}
void MusicGlass::suspendCapture(bool suspend){
    if(!d->active.load())return;
    if(suspend){
        ++d->suspendCount;
        {std::lock_guard<std::mutex> lock(d->requestMutex);d->request.frozen=true;}
        d->wake.notify_all();
        // A popup may appear immediately after this method returns. Wait for an
        // already running BitBlt to finish before letting it become visible.
        std::lock_guard<std::mutex> capture(d->captureMutex);
    }else{
        const quint64 gen=d->generation.load();
        // Hide/aboutToHide precedes the native popup actually disappearing.
        QTimer::singleShot(60,this,[this,gen]{
            if(!d->active.load()||d->generation.load()!=gen)return;
            const int count=d->suspendCount.load();if(count<=0)return;
            d->suspendCount=count-1;
            {std::lock_guard<std::mutex> lock(d->requestMutex);d->request.frozen=d->frozen.load()||d->suspendCount.load()>0;}
            d->wake.notify_all();
        });
    }
}
bool MusicGlass::eventFilter(QObject *watched,QEvent *event){
    if(!d->active.load())return false;
    if(watched==d->window){
        if(event->type()==QEvent::Move||event->type()==QEvent::Resize||event->type()==QEvent::Show||event->type()==QEvent::Hide||event->type()==QEvent::WindowStateChange)updateRequest();
        else if(event->type()==QEvent::PlatformSurface){auto *surface=static_cast<QPlatformSurfaceEvent*>(event);if(surface->surfaceEventType()==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)stop();}
        return false;
    }
    auto *widget=qobject_cast<QWidget*>(watched);
    if(!widget||!widget->isWindow())return false;
    const auto type=widget->windowType();
    const bool popup=qobject_cast<QMenu*>(widget)||qobject_cast<QDialog*>(widget)||type==Qt::Popup||type==Qt::ToolTip;
    if(!popup)return false;
    if(event->type()==QEvent::Show&&!d->transients.contains(widget)){
        d->transients.insert(widget,QPointer<QWidget>(widget));
        suspendCapture(true);
        if(!d->observedTransientLifetimes.contains(widget)){
            d->observedTransientLifetimes.insert(widget);
            connect(widget,&QObject::destroyed,this,[this,widget]{
                d->observedTransientLifetimes.remove(widget);
                if(d->transients.remove(widget)>0)suspendCapture(false);
            });
        }
    }else if((event->type()==QEvent::Hide||event->type()==QEvent::Destroy)&&d->transients.remove(widget)>0){
        suspendCapture(false);
    }
    return false;
}
