#include <QtWidgets>
#include <QSaveFile>
#include <QPainter>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include "CodeEditor.h"
#include "LspClient.h"
#include "WelcomeWindow.h"
#include "GlassEffect.h"

static EditorColors colors;
static QColor windowBg("#10131e"), panelBg("#171b29"), iconColor("#a5b4d5");
static int fontSize = 13;
static int opacityPercent = 35;
static bool animateLines = true;
static bool transparentEditor = true;
static QSettings settings("REWORK", "REWORK Code");

static QColor loadColor(const char *key, const QColor &fallback) {
    QColor c(settings.value(QString("colors/") + key, fallback.name()).toString());
    return c.isValid() ? c : fallback;
}
static void loadSettings() {
    colors.background = loadColor("editorBackground", QColor("#111522"));
    colors.foreground = loadColor("text", QColor("#dce6f8"));
    colors.lineNumber = loadColor("lineNumber", QColor("#63738f"));
    colors.currentLine = loadColor("currentLine", QColor("#20283c"));
    colors.keyword = loadColor("keyword", QColor("#c299ff"));
    colors.stringLiteral = loadColor("string", QColor("#9ae5b0"));
    colors.comment = loadColor("comment", QColor("#71859a"));
    colors.number = loadColor("number", QColor("#ffbf83"));
    colors.function = loadColor("function", QColor("#89d3fa"));
    colors.variable = loadColor("variable", QColor("#dce6f8"));
    colors.accent = loadColor("accent", QColor("#8f9dff"));
    windowBg = loadColor("window", windowBg); panelBg = loadColor("panel", panelBg); iconColor = loadColor("icons", iconColor);
    fontSize = qBound(9, settings.value("fontSize", 13).toInt(), 26);
    opacityPercent = qBound(25, settings.value("opacity", 35).toInt(), 100);
    animateLines = settings.value("animateLines", true).toBool();
    transparentEditor = settings.value("transparentEditor", true).toBool();
}
static void persistSettings() {
    const QList<QPair<QString,QColor>> items = {{"editorBackground",colors.background},{"text",colors.foreground},{"lineNumber",colors.lineNumber},{"currentLine",colors.currentLine},{"keyword",colors.keyword},{"string",colors.stringLiteral},{"comment",colors.comment},{"number",colors.number},{"function",colors.function},{"variable",colors.variable},{"accent",colors.accent},{"window",windowBg},{"panel",panelBg},{"icons",iconColor}};
    for (auto p : items) settings.setValue("colors/"+p.first,p.second.name());
    settings.setValue("fontSize",fontSize); settings.setValue("opacity",opacityPercent); settings.setValue("animateLines",animateLines); settings.setValue("transparentEditor",transparentEditor);
}

class ToggleSwitch : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
    QSize sizeHint() const override { return QSize(46, 26); }
protected:
    bool hitButton(const QPoint &pos) const override { return rect().contains(pos); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QRectF track(1, 3, 42, 20);
        p.setPen(isChecked() ? QColor("#9fb1ff") : QColor("#62718b"));
        p.setBrush(isChecked() ? QColor("#657ed6") : QColor("#344055"));
        p.drawRoundedRect(track, 10, 10);
        p.setPen(Qt::NoPen); p.setBrush(isChecked() ? QColor("#ffffff") : QColor("#b2bdce"));
        p.drawEllipse(QRectF(isChecked()?24:4, 6, 14, 14));
        if(hasFocus()){p.setPen(QPen(QColor("#aec1ff"), 1, Qt::DotLine));p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(0,1,44,24),11,11);}
    }
};

class SettingsDialog : public QDialog {
public:
    SettingsDialog(QWidget *parent=nullptr):QDialog(parent) {
        setWindowTitle(QString::fromUtf8("Оформление — REWORK Code")); resize(520,700);
        auto *outer = new QVBoxLayout(this); auto *heading = new QLabel(QString::fromUtf8("ОФОРМЛЕНИЕ")); heading->setStyleSheet("font-size: 20px; font-weight: 700;"); outer->addWidget(heading);
        auto *hint = new QLabel(QString::fromUtf8("Цвета редактора и интерфейса")); outer->addWidget(hint);
        auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); auto *content = new QWidget; auto *form = new QFormLayout(content);
        addColor(form,QString::fromUtf8("Фон окна"),&windowBg); addColor(form,QString::fromUtf8("Панели"),&panelBg); addColor(form,QString::fromUtf8("Иконки и вторичный текст"),&iconColor);
        addColor(form,QString::fromUtf8("Фон кода"),&colors.background); addColor(form,QString::fromUtf8("Основной текст"),&colors.foreground); addColor(form,QString::fromUtf8("Переменные / идентификаторы"),&colors.variable); addColor(form,QString::fromUtf8("Ключевые слова"),&colors.keyword); addColor(form,QString::fromUtf8("Строки"),&colors.stringLiteral); addColor(form,QString::fromUtf8("Комментарии"),&colors.comment); addColor(form,QString::fromUtf8("Числа"),&colors.number); addColor(form,QString::fromUtf8("Функции"),&colors.function); addColor(form,QString::fromUtf8("Номера строк"),&colors.lineNumber); addColor(form,QString::fromUtf8("Текущая строка"),&colors.currentLine); addColor(form,QString::fromUtf8("Акцент"),&colors.accent);
        auto *font = new QSpinBox; font->setRange(9,26); font->setValue(fontSize); form->addRow(QString::fromUtf8("Размер шрифта"),font);
        auto *op = new QSpinBox; op->setRange(25,100); op->setSuffix(" %"); op->setValue(opacityPercent); form->addRow(QString::fromUtf8("Непрозрачность стекла"),op);
        auto *glassToggle = new ToggleSwitch; glassToggle->setAccessibleName(QString::fromUtf8("Прозрачная область кода")); glassToggle->setChecked(transparentEditor); form->addRow(QString::fromUtf8("Прозрачная область кода"), glassToggle);
        auto *anim = new QCheckBox(QString::fromUtf8("Плавный переход текста при Enter")); anim->setChecked(animateLines); form->addRow(anim);
        scroll->setWidget(content); outer->addWidget(scroll); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); outer->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,this,[=]{fontSize=font->value();opacityPercent=op->value();animateLines=anim->isChecked();transparentEditor=glassToggle->isChecked();accept();}); connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    }
private:
    void addColor(QFormLayout *form, const QString &name, QColor *target) {
        auto *button = new QPushButton(target->name()); button->setMinimumWidth(115); auto paint = [button,target]{button->setText(target->name()); button->setStyleSheet(QString("background:%1;color:%2;border:1px solid #68738c;padding:5px;").arg(target->name(),target->lightness()>125?"#101010":"#ffffff"));}; paint();
        connect(button,&QPushButton::clicked,this,[this,target,paint]{QColor chosen=QColorDialog::getColor(*target,this);if(chosen.isValid()){*target=chosen;paint();}}); form->addRow(name,button);
    }
};

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowTitle("REWORK Code"); resize(1440,900); setMinimumSize(850,550);
        auto *central=new QWidget; auto *layout=new QVBoxLayout(central); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
        auto *top=new QFrame; top->setObjectName("top"); auto *bar=new QHBoxLayout(top); bar->setContentsMargins(20,12,20,12);
        auto *logo=new QLabel("REWORK <span style='color:#96aaff'>CODE</span>"); logo->setStyleSheet("font-size:17px;font-weight:800;letter-spacing:2px;"); bar->addWidget(logo); auto *tag=new QLabel("  /  C++ WORKSPACE"); tag->setObjectName("muted"); bar->addWidget(tag); bar->addStretch();
        auto *createProjectButton=new QPushButton(QString::fromUtf8("+ Проект")); bar->addWidget(createProjectButton); auto *openButton=new QPushButton(QString::fromUtf8("Открыть проект")); auto *newButton=new QPushButton(QString::fromUtf8("+ Файл")); auto *settingButton=new QPushButton(QString::fromUtf8("⚙  Настройки")); bar->addWidget(openButton); bar->addWidget(newButton); bar->addWidget(settingButton); layout->addWidget(top);
        auto *split=new QSplitter; tree=new QTreeView; model=new QFileSystemModel(this); model->setFilter(QDir::AllDirs|QDir::Files|QDir::NoDotAndDotDot); tree->setModel(model); tree->setHeaderHidden(true); for(int i=1;i<4;++i) tree->hideColumn(i); tree->setMinimumWidth(180); tree->setMaximumWidth(520);
        auto *side=new QWidget; auto *sideLayout=new QVBoxLayout(side); sideLayout->setContentsMargins(12,15,8,10); auto *label=new QLabel(QString::fromUtf8("ПРОВОДНИК")); label->setObjectName("section"); sideLayout->addWidget(label); projectLabel=new QLabel(QString::fromUtf8("Открой папку проекта")); projectLabel->setWordWrap(true); projectLabel->setObjectName("muted"); sideLayout->addWidget(projectLabel); sideLayout->addWidget(tree); split->addWidget(side);
        tabs=new QTabWidget; tabs->setTabsClosable(true); tabs->setMovable(true); tabs->setDocumentMode(true); split->addWidget(tabs); split->setStretchFactor(1,1); split->setSizes({260,1100}); layout->addWidget(split,1); setCentralWidget(central);
        status=new QLabel(QString::fromUtf8("Готово")); statusBar()->addPermanentWidget(status,1);
        lsp=new LspClient(this);
        connect(lsp,&LspClient::statusChanged,this,[this](const QString &s){status->setText(s);});
        connect(lsp,&LspClient::diagnosticsReceived,this,[this](const QString &path,const QJsonArray &items){ if(current() && current()->property("path").toString()==path) status->setText(QString::fromUtf8("clangd: диагностик — %1").arg(items.size())); });
        connect(lsp,&LspClient::completionReceived,this,[this](int id,const QJsonValue &value){
            if(id!=completionId || !completionEditor || completionEditor!=current() || completionEditor->document()->revision()!=completionRevision || completionEditor->textCursor().position()!=completionPosition) return;
            QPointer<CodeEditor> editor=completionEditor;
            const int revision=completionRevision, position=completionPosition;
            const QJsonObject list=value.toObject(), defaults=list.value("itemDefaults").toObject();
            const QJsonArray items=value.isArray()?value.toArray():list.value("items").toArray();
            QMenu menu(this);
            int count=0;
            for(const auto &v:items){
                const QJsonObject obj=v.toObject();
                const QString label=obj.value("label").toString();
                const QJsonValue extras=obj.value("additionalTextEdits");
                const int format=obj.value("insertTextFormat").toInt(defaults.value("insertTextFormat").toInt(1));
                if(label.isEmpty() || format!=1 || (!extras.isUndefined() && (!extras.isArray() || !extras.toArray().isEmpty())) || obj.contains("command")) continue;
                if(!obj.contains("textEdit") && defaults.contains("editRange")) continue;
                if(obj.contains("textEdit") && !obj.value("textEdit").isObject()) continue;
                QAction *a=menu.addAction(label); a->setData(obj.toVariantMap());
                if(++count>=40) break;
            }
            if(menu.isEmpty()){status->setText(QString::fromUtf8("clangd: нет поддерживаемых подсказок"));return;}
            QAction *selected=menu.exec(editor->mapToGlobal(editor->cursorRect().bottomLeft()));
            if(!selected || id!=completionId || !editor || completionEditor!=editor || editor!=current() || editor->document()->revision()!=revision || editor->textCursor().position()!=position) return;
            const QJsonObject item=QJsonObject::fromVariantMap(selected->data().toMap());
            QTextCursor cursor=editor->textCursor();
            int start=position, end=position;
            QString insert;
            if(item.contains("textEdit")){
                const QJsonObject edit=item.value("textEdit").toObject();
                if(!edit.value("newText").isString()) return;
                insert=edit.value("newText").toString();
                const QJsonObject range=(edit.contains("range")?edit.value("range"):edit.value("replace")).toObject();
                auto offset=[editor](const QJsonObject &p)->int{
                    const int line=p.value("line").toInt(-1), character=p.value("character").toInt(-1);
                    if(line<0 || character<0) return -1;
                    QTextCursor c(editor->document()); c.movePosition(QTextCursor::Start);
                    if(line>0 && !c.movePosition(QTextCursor::NextBlock,QTextCursor::MoveAnchor,line)) return -1;
                    const int beginning=c.position(); c.movePosition(QTextCursor::EndOfBlock);
                    return character<=c.position()-beginning?beginning+character:-1;
                };
                const QJsonObject from=range.value("start").toObject(), to=range.value("end").toObject();
                if(from.value("line").toInt(-1)!=cursor.blockNumber() || to.value("line").toInt(-1)!=cursor.blockNumber()) return;
                start=offset(from); end=offset(to);
                if(start<0 || end<start || start>position || end<position) return;
            }else{
                insert=item.value("insertText").toString(item.value("label").toString());
                while(start>0){const QChar ch=editor->document()->characterAt(start-1);if(!ch.isLetterOrNumber() && ch!='_')break;--start;}
            }
            cursor.setPosition(start); cursor.setPosition(end,QTextCursor::KeepAnchor); cursor.insertText(insert); editor->setTextCursor(cursor);
        });
        connect(createProjectButton,&QPushButton::clicked,this,[this]{newProject();}); connect(openButton,&QPushButton::clicked,this,[this]{openProject();}); connect(newButton,&QPushButton::clicked,this,[this]{newFile();}); connect(settingButton,&QPushButton::clicked,this,[this]{editSettings();}); connect(tabs,&QTabWidget::tabCloseRequested,this,[this](int i){closeTab(i);});
        connect(tree,&QTreeView::doubleClicked,this,[this](const QModelIndex &idx){if(!model->isDir(idx))openFile(model->filePath(idx));});
        createMenus(); applyTheme(); newFile();
    }
public:
    void enableGlass(){glassActive=GlassEffect::applyGlass(this);applyTheme();}
    bool startNewProject(){newProject();return !projectPath.isEmpty();}
    bool startOpenProject(){openProject();return !projectPath.isEmpty();}
    bool startRecentProject(const QString &dir){if(!QFileInfo(dir).isDir()){QMessageBox::warning(QApplication::activeWindow(),QString::fromUtf8("Папка недоступна"),QString::fromUtf8("Папка проекта больше не найдена: ")+dir);return false;}setProject(dir);return true;}
protected:
    void closeEvent(QCloseEvent *event) override { while(tabs->count()) if(!closeTab(0)){event->ignore();return;} lsp->stop(); event->accept(); }
private:
    bool glassActive=false; QTreeView *tree; QFileSystemModel *model; QTabWidget *tabs; QLabel *status; QLabel *projectLabel; LspClient *lsp; QString projectPath; int completionId=-1; int completionRevision=-1; int completionPosition=-1; QPointer<CodeEditor> completionEditor;
    CodeEditor *current() const {return qobject_cast<CodeEditor*>(tabs->currentWidget());}
    void createMenus() {
        auto *file=menuBar()->addMenu(QString::fromUtf8("Файл"));
        auto action=[this,file](const QString &name,const QKeySequence &shortcut,auto fn){auto *a=file->addAction(name); a->setShortcut(shortcut);connect(a,&QAction::triggered,this,fn);};
        action(QString::fromUtf8("Новый файл"),QKeySequence::New,[this]{newFile();}); action(QString::fromUtf8("Открыть файл…"),QKeySequence::Open,[this]{QString p=QFileDialog::getOpenFileName(this,QString::fromUtf8("Открыть файл"),projectPath);if(!p.isEmpty())openFile(p);});
        action(QString::fromUtf8("Создать проект…"),QKeySequence("Ctrl+Shift+N"),[this]{newProject();}); action(QString::fromUtf8("Открыть папку проекта…"),QKeySequence("Ctrl+Shift+O"),[this]{openProject();}); file->addSeparator(); action(QString::fromUtf8("Сохранить"),QKeySequence::Save,[this]{saveCurrent(false);}); action(QString::fromUtf8("Сохранить как…"),QKeySequence::SaveAs,[this]{saveCurrent(true);}); action(QString::fromUtf8("Закрыть вкладку"),QKeySequence::Close,[this]{closeTab(tabs->currentIndex());});
        auto *edit=menuBar()->addMenu(QString::fromUtf8("Правка")); auto *find=edit->addAction(QString::fromUtf8("Найти…")); find->setShortcut(QKeySequence::Find); connect(find,&QAction::triggered,this,[this]{if(!current())return;bool ok=false;QString text=QInputDialog::getText(this,QString::fromUtf8("Поиск"),QString::fromUtf8("Найти:"),QLineEdit::Normal,{},&ok);if(ok&&!text.isEmpty()&&!current()->find(text)){current()->moveCursor(QTextCursor::Start);if(!current()->find(text))QMessageBox::information(this,QString::fromUtf8("Поиск"),QString::fromUtf8("Совпадений нет."));}});
        auto *complete=edit->addAction(QString::fromUtf8("Подсказки clangd"));complete->setShortcut(QKeySequence("Ctrl+Space"));connect(complete,&QAction::triggered,this,[this]{requestCompletion();});
        auto *view=menuBar()->addMenu(QString::fromUtf8("Вид"));connect(view->addAction(QString::fromUtf8("Настройки оформления…")),&QAction::triggered,this,[this]{editSettings();});
    }
    void applyTheme(bool resetFont = false) {
        setWindowOpacity(glassActive?1.0:qMax(0.75,opacityPercent/100.0));
        auto surface=[this](const QColor &c,int alpha){return glassActive?QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha):c.name();};
        const QString windowSurface=surface(windowBg,qRound(opacityPercent*2.55));
        const QString panelSurface=surface(panelBg,115);
        setStyleSheet(QString(R"(
QMainWindow {background:%1;} QWidget {background:transparent;color:%4;font-family:'Segoe UI';font-size:13px;}
QFrame#top {background:%2;border-bottom:1px solid #303a53;}
QLabel#muted {color:%3;font-size:11px;}
QLabel#section {color:%3;font-size:10px;font-weight:700;letter-spacing:2px;}
QPushButton {background:%2;border:1px solid #39445e;border-radius:7px;padding:7px 12px;}
QPushButton:hover {border-color:%5;background:#28324a;}
QTreeView,QScrollArea {background:transparent;border:0;outline:0;} QTreeView::item {padding:5px;border-radius:4px;} QTreeView::item:selected {background:#303957;}
QTabWidget::pane {border:0;} QTabBar::tab {background:%2;color:%3;padding:10px 18px;border:0;border-bottom:2px solid transparent;} QTabBar::tab:selected {color:%4;border-bottom:2px solid %5;background:%1;}
QMenuBar,QMenu,QStatusBar {background:%2;color:%4;} QMenu::item:selected,QMenuBar::item:selected {background:#333e59;} QSpinBox {background:%2;padding:5px;border:1px solid #48536c;}
)").arg(windowSurface,panelSurface,iconColor.name(),colors.foreground.name(),colors.accent.name()));
        for(int i=0;i<tabs->count();++i) {auto *e=qobject_cast<CodeEditor*>(tabs->widget(i)); if(e){e->setEditorColors(colors); e->setTransparentBackground(transparentEditor && glassActive); e->setLineAnimationEnabled(animateLines); if(resetFont)e->setEditorFontSize(fontSize);}}
    }
    void editSettings(){EditorColors old=colors;QColor oldWindow=windowBg,oldPanel=panelBg,oldIcons=iconColor;int oldFont=fontSize,oldOp=opacityPercent;SettingsDialog dlg(this); if(dlg.exec()==QDialog::Accepted){persistSettings();applyTheme(oldFont!=fontSize);}else{colors=old;windowBg=oldWindow;panelBg=oldPanel;iconColor=oldIcons;fontSize=oldFont;opacityPercent=oldOp;}}
    void setupEditor(CodeEditor *e){e->setProperty("path",QString());e->setEditorColors(colors);e->setTransparentBackground(transparentEditor && glassActive);e->setLineAnimationEnabled(animateLines);e->setEditorFontSize(fontSize);connect(e->document(),&QTextDocument::modificationChanged,this,[this,e](bool){updateTab(e);});auto *timer=new QTimer(e);timer->setSingleShot(true);timer->setInterval(220);connect(e,&QPlainTextEdit::textChanged,timer,qOverload<>(&QTimer::start));connect(timer,&QTimer::timeout,this,[this,e]{QString p=e->property("path").toString();if(!p.isEmpty())lsp->changeDocument(p,e->toPlainText());});}
    void updateTab(CodeEditor *e){int i=tabs->indexOf(e);if(i<0)return;QString p=e->property("path").toString();QString name=p.isEmpty()?QString::fromUtf8("Без имени"):QFileInfo(p).fileName();if(e->document()->isModified())name+=" •";tabs->setTabText(i,name);tabs->setTabToolTip(i,p);}
    void newFile(){auto *e=new CodeEditor;setupEditor(e);int i=tabs->addTab(e,QString::fromUtf8("Без имени"));tabs->setCurrentIndex(i);e->setFocus();}
    void newProject(){QWidget *owner=isVisible()?this:QApplication::activeWindow();QString parent=QFileDialog::getExistingDirectory(owner,QString::fromUtf8("Выберите папку для нового проекта"),projectPath);if(parent.isEmpty())return;bool ok=false;QString name=QInputDialog::getText(owner,QString::fromUtf8("Новый проект"),QString::fromUtf8("Имя проекта:"),QLineEdit::Normal,{},&ok).trimmed();if(!ok||name.isEmpty())return;if(name=="."||name==".."||name.contains(QRegularExpression(QStringLiteral(R"([\\/:*?\"<>|])")))){QMessageBox::warning(owner,QString::fromUtf8("Неверное имя"),QString::fromUtf8("Введите допустимое имя папки."));return;}QDir base(parent);if(base.exists(name)){QMessageBox::warning(owner,QString::fromUtf8("Папка существует"),QString::fromUtf8("Выберите другое имя проекта."));return;}if(!base.mkdir(name)){QMessageBox::warning(owner,QString::fromUtf8("Ошибка"),QString::fromUtf8("Не удалось создать папку проекта."));return;}setProject(base.filePath(name));} void openProject(){QWidget *owner=isVisible()?this:QApplication::activeWindow();QString dir=QFileDialog::getExistingDirectory(owner,QString::fromUtf8("Открыть папку проекта"),projectPath);if(!dir.isEmpty())setProject(dir);} void setProject(const QString &dir){projectPath=QDir::cleanPath(QFileInfo(dir).absoluteFilePath());QStringList recent=settings.value("recentProjects").toStringList();for(int i=recent.size()-1;i>=0;--i)if(recent[i].compare(projectPath,Qt::CaseInsensitive)==0)recent.removeAt(i);recent.prepend(projectPath);while(recent.size()>20)recent.removeLast();settings.setValue("recentProjects",recent);model->setRootPath(projectPath);tree->setRootIndex(model->index(projectPath));projectLabel->setText(QFileInfo(projectPath).fileName());lsp->stop();lsp->start(projectPath, QFileInfo::exists("C:/Qt/Tools/QtCreator/bin/clang/bin/clangd.exe") ? QStringLiteral("C:/Qt/Tools/QtCreator/bin/clang/bin/clangd.exe") : QString());for(int i=0;i<tabs->count();++i){auto *e=qobject_cast<CodeEditor*>(tabs->widget(i));QString p=e->property("path").toString();if(!p.isEmpty())lsp->openDocument(p,e->toPlainText());}}
    void openFile(const QString &path){QString p=QFileInfo(path).absoluteFilePath();for(int i=0;i<tabs->count();++i){auto *e=qobject_cast<CodeEditor*>(tabs->widget(i));if(e->property("path").toString().compare(p,Qt::CaseInsensitive)==0){tabs->setCurrentIndex(i);return;}} QFile f(p);if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,QString::fromUtf8("Не удалось открыть"),f.errorString());return;}QByteArray data=f.readAll();if(f.error()!=QFileDevice::NoError){QMessageBox::warning(this,QString::fromUtf8("Ошибка чтения"),f.errorString());return;}if(data.contains('\0')){QMessageBox::warning(this,QString::fromUtf8("Не удалось открыть"),QString::fromUtf8("Похоже, это бинарный файл."));return;}bool bom=data.startsWith("\xEF\xBB\xBF");QByteArray payload=bom?data.mid(3):data;QString text=QString::fromUtf8(payload);if(text.toUtf8()!=payload){QMessageBox::warning(this,QString::fromUtf8("Кодировка"),QString::fromUtf8("Файл не является корректным UTF-8. Открытие отменено, чтобы не повредить данные."));return;}auto *e=new CodeEditor;setupEditor(e);e->setPlainText(text);e->setProperty("path",p);e->setProperty("bom",bom);e->setProperty("crlf",payload.contains("\r\n"));e->setProperty("diskHash",QCryptographicHash::hash(data,QCryptographicHash::Sha256));e->document()->setModified(false);tabs->setCurrentIndex(tabs->addTab(e,QFileInfo(p).fileName()));updateTab(e);lsp->openDocument(p,text);e->setFocus();}
    bool saveCurrent(bool as){auto *e=current();if(!e)return false;QString old=e->property("path").toString();QString path=old;if(path.isEmpty()||as){path=QFileDialog::getSaveFileName(this,QString::fromUtf8("Сохранить файл"),projectPath.isEmpty()?QString("untitled.cpp"):projectPath+"/untitled.cpp",QString::fromUtf8("Исходный код C++ (*.cpp *.h *.hpp *.cc);;Все файлы (*.*)"));if(path.isEmpty())return false;path=QFileInfo(path).absoluteFilePath();} for(int i=0;i<tabs->count();++i){auto *other=qobject_cast<CodeEditor*>(tabs->widget(i));if(other!=e && other->property("path").toString().compare(path,Qt::CaseInsensitive)==0){QMessageBox::warning(this,QString::fromUtf8("Файл уже открыт"),QString::fromUtf8("Этот файл уже открыт в другой вкладке."));return false;}} if(path==old && e->property("diskHash").isValid()){QFile disk(path);if(!disk.open(QIODevice::ReadOnly) || QCryptographicHash::hash(disk.readAll(),QCryptographicHash::Sha256)!=e->property("diskHash").toByteArray()){auto answer=QMessageBox::warning(this,QString::fromUtf8("Файл изменился"),QString::fromUtf8("Файл на диске изменился или недоступен. Перезаписать его текущей версией?"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);if(answer!=QMessageBox::Yes)return false;}} QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){QMessageBox::warning(this,QString::fromUtf8("Ошибка сохранения"),file.errorString());return false;}QString output=e->toPlainText();if(e->property("crlf").toBool())output.replace("\n","\r\n");QByteArray bytes=output.toUtf8();if(e->property("bom").toBool())bytes.prepend("\xEF\xBB\xBF");if(file.write(bytes)!=bytes.size() || !file.commit()){QMessageBox::warning(this,QString::fromUtf8("Ошибка сохранения"),file.errorString());return false;}e->setProperty("diskHash",QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));if(old!=path){if(!old.isEmpty())lsp->closeDocument(old);e->setProperty("path",path);lsp->openDocument(path,e->toPlainText());}e->document()->setModified(false);updateTab(e);status->setText(QString::fromUtf8("Сохранено: ")+QFileInfo(path).fileName());return true;}
    bool closeTab(int i){if(i<0)return true;auto *e=qobject_cast<CodeEditor*>(tabs->widget(i));if(e->document()->isModified()){tabs->setCurrentIndex(i);auto answer=QMessageBox::question(this,QString::fromUtf8("Несохранённые изменения"),QString::fromUtf8("Сохранить изменения в «%1»?").arg(tabs->tabText(i)),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel);if(answer==QMessageBox::Cancel)return false;if(answer==QMessageBox::Save&&!saveCurrent(false))return false;}QString p=e->property("path").toString();if(!p.isEmpty())lsp->closeDocument(p);tabs->removeTab(i);e->deleteLater();return true;}
    void requestCompletion(){auto *e=current();if(!e)return;QString p=e->property("path").toString();if(p.isEmpty()){status->setText(QString::fromUtf8("Сначала сохраните файл для подсказок clangd"));return;}lsp->changeDocument(p,e->toPlainText());QTextCursor c=e->textCursor();completionEditor=e;completionRevision=e->document()->revision();completionPosition=c.position();completionId=lsp->requestCompletion(p,c.blockNumber(),c.positionInBlock());}
};
int main(int argc,char **argv){
    QApplication app(argc,argv);
    QApplication::setApplicationName("REWORK Code");
    QApplication::setWindowIcon(QIcon(":/assets/rework-code.ico"));
    QApplication::setOrganizationName("REWORK");
    loadSettings();
    MainWindow w;
    WelcomeWindow welcome;
    welcome.setRecentProjects(settings.value("recentProjects").toStringList());
    auto enterWorkspace=[&]{w.show(); w.enableGlass(); welcome.hide();};
    QObject::connect(&welcome,&WelcomeWindow::continueRequested,&w,enterWorkspace);
    QObject::connect(&welcome,&WelcomeWindow::createProjectRequested,&w,[&]{if(w.startNewProject())enterWorkspace();});
    QObject::connect(&welcome,&WelcomeWindow::openProjectRequested,&w,[&]{if(w.startOpenProject())enterWorkspace();});
    QObject::connect(&welcome,&WelcomeWindow::projectSelected,&w,[&](const QString &path){if(w.startRecentProject(path))enterWorkspace();});
    welcome.setAttribute(Qt::WA_TranslucentBackground);
    welcome.show();
    if (GlassEffect::applyGlass(&welcome)) {
        QString style=welcome.styleSheet();
        style.replace("background: #101521; color: #e6ecff", "background: rgba(16,21,33,80); color: #e6ecff");
        style.replace("background: #171e2c; border: 1px", "background: rgba(23,30,44,130); border: 1px");
        welcome.setStyleSheet(style);
    }
    return app.exec();
}



