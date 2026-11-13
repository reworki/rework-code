#include "CodeEditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QImage>
#include <QtMath>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>

class SyntaxHighlighter : public QSyntaxHighlighter {
public:
    explicit SyntaxHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {}
    void setColors(const EditorColors &colors) { m_colors = colors; rehighlight(); }
protected:
    void highlightBlock(const QString &text) override {
        auto apply = [&](const QRegularExpression &re, const QColor &color, bool bold = false) {
            QTextCharFormat fmt; fmt.setForeground(color);
            if (bold) fmt.setFontWeight(QFont::DemiBold);
            auto matches = re.globalMatch(text);
            while (matches.hasNext()) { const auto m = matches.next(); setFormat(m.capturedStart(), m.capturedLength(), fmt); }
        };
        static const QRegularExpression ident(QStringLiteral(R"(\b[A-Za-z_][A-Za-z_0-9]*\b)"));
        static const QRegularExpression keywords(QStringLiteral(R"(\b(alignas|alignof|asm|auto|bool|break|case|catch|char|char8_t|char16_t|char32_t|class|const|consteval|constexpr|constinit|const_cast|continue|co_await|co_return|co_yield|decltype|default|delete|do|double|dynamic_cast|else|enum|explicit|export|extern|false|float|for|friend|goto|if|inline|int|long|mutable|namespace|new|noexcept|nullptr|operator|private|protected|public|register|reinterpret_cast|requires|return|short|signed|sizeof|static|static_assert|static_cast|struct|switch|template|this|thread_local|throw|true|try|typedef|typeid|typename|union|unsigned|using|virtual|void|volatile|wchar_t|while)\b)"));
        static const QRegularExpression numbers(QStringLiteral(R"(\b(?:0[xX][0-9A-Fa-f']+|0[bB][01']+|\d[\d']*(?:\.\d[\d']*)?(?:[eE][+-]?\d+)?)(?:[uUlLfF]+)?\b)"));
        static const QRegularExpression functions(QStringLiteral(R"(\b[A-Za-z_][A-Za-z_0-9]*(?=\s*\())"));
        static const QRegularExpression preprocessor(QStringLiteral(R"(^\s*#\s*[A-Za-z_]+)"));
        apply(ident, m_colors.variable);
        apply(functions, m_colors.function);
        apply(numbers, m_colors.number);
        apply(keywords, m_colors.keyword, true);
        apply(preprocessor, m_colors.keyword);

        // Scan strings and comments together so comment markers inside strings remain strings.
        QTextCharFormat commentFmt; commentFmt.setForeground(m_colors.comment);
        QTextCharFormat stringFmt; stringFmt.setForeground(m_colors.stringLiteral);
        setCurrentBlockState(0);
        int i = 0;
        if (previousBlockState() == 1) {
            const int end = text.indexOf(QStringLiteral("*/"));
            if (end < 0) { setFormat(0, text.size(), commentFmt); setCurrentBlockState(1); return; }
            setFormat(0, end + 2, commentFmt); i = end + 2;
        }
        while (i < text.size()) {
            if (i + 1 < text.size() && text.at(i) == QLatin1Char('/') && text.at(i + 1) == QLatin1Char('/')) {
                setFormat(i, text.size() - i, commentFmt); break;
            }
            if (i + 1 < text.size() && text.at(i) == QLatin1Char('/') && text.at(i + 1) == QLatin1Char('*')) {
                const int end = text.indexOf(QStringLiteral("*/"), i + 2);
                if (end < 0) { setFormat(i, text.size() - i, commentFmt); setCurrentBlockState(1); break; }
                setFormat(i, end + 2 - i, commentFmt); i = end + 2; continue;
            }
            if (text.at(i) == QLatin1Char('"') || text.at(i) == QLatin1Char('\'')) {
                const QChar quote = text.at(i); const int start = i++;
                while (i < text.size()) {
                    if (text.at(i) == QLatin1Char('\\')) { i += qMin(2, text.size() - i); continue; }
                    if (text.at(i++) == quote) break;
                }
                setFormat(start, i - start, stringFmt); continue;
            }
            ++i;
        }
    }
private:
    EditorColors m_colors;
};

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }
private:
    CodeEditor *m_editor;
};


// This overlay animates a snapshot only.  The document changes synchronously, so
// undo, saving and subsequent keystrokes never depend on an animation timer.
class LineTransitionOverlay : public QWidget {
public:
    explicit LineTransitionOverlay(QWidget *parent) : QWidget(parent), animation(new QVariantAnimation(this)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        animation->setDuration(190);
        animation->setStartValue(0.0); animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v){progress=v.toReal();update();});
        connect(animation, &QVariantAnimation::finished, this, [this]{hide();before=QImage();after=QImage();tail=QImage();});
        hide();
    }
    void cancel() { animation->stop(); hide(); before=QImage(); after=QImage(); tail=QImage(); }
    void start(const QImage &oldImage, const QImage &newImage, const QRect &oldCursor, const QRect &tailDestination, const QColor &lineBg, const QColor &bg, bool transparent) {
        cancel();
        if(oldImage.isNull() || newImage.isNull() || oldImage.size()!=newImage.size()) return;
        before=oldImage;after=newImage; background=bg; transparentBackground=transparent; oldPos=oldCursor.topLeft(); targetPos=tailDestination.topLeft();
        lineHeight=qMax(oldCursor.height(),tailDestination.height());
        lowerStart=oldCursor.y()+lineHeight;
        lowerDistance=tailDestination.y()-oldCursor.y();
        if(lowerDistance<=0 || lineHeight<=0) return;
        QRect cut(oldCursor.x()+2,oldCursor.y(),oldImage.width()-oldCursor.x()-2,lineHeight);
        cut=cut.intersected(oldImage.rect());
        if(!cut.isEmpty()) {
            tail=oldImage.copy(cut).convertToFormat(QImage::Format_ARGB32);
            // The current-line fill is made transparent so the moving code can
            // be drawn over the destination without an opaque rectangular patch.
            for(int y=0;y<tail.height();++y) {
                QRgb *row=reinterpret_cast<QRgb*>(tail.scanLine(y));
                for(int x=0;x<tail.width();++x) {
                    const QColor c=QColor::fromRgba(row[x]);
                    const int delta=qMax(qAbs(c.red()-lineBg.red()),qMax(qAbs(c.green()-lineBg.green()),qAbs(c.blue()-lineBg.blue())));
                    if(delta<14) row[x]=qRgba(0,0,0,0);
                }
            }
        }
        setGeometry(parentWidget()->rect()); progress=0;show();raise();animation->start();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.drawImage(QPoint(0,0),after);
        if(lowerStart<height()) {
            const int dest=lowerStart+lowerDistance;
            if (!transparentBackground) p.fillRect(QRect(0,lowerStart,width(),height()-lowerStart),background);
            const int y=qRound(lowerStart+lowerDistance*progress);
            const QRect source(0,lowerStart,width(),height()-lowerStart);
            p.drawImage(QRect(0,y,width(),source.height()),before,source);
            Q_UNUSED(dest);
        }
        // Erase the destination suffix until its snapshot has reached it.
        const QRect destination(targetPos.x(),targetPos.y(),width()-targetPos.x(),lineHeight);
        if (!transparentBackground) p.fillRect(destination,background);
        if(!tail.isNull()) {
            const int x=qRound(oldPos.x()+(targetPos.x()-oldPos.x())*progress);
            const int y=qRound(oldPos.y()+(targetPos.y()-oldPos.y())*progress);
            p.drawImage(QPoint(x,y),tail);
        }
    }
private:
    QVariantAnimation *animation;
    QImage before,after,tail;
    QColor background;
    QPoint oldPos,targetPos;
    int lineHeight=0,lowerStart=0,lowerDistance=0;
    qreal progress=0;
    bool transparentBackground=false;
};

// Minimal C++ lexical context; ignore pair insertion inside strings or comments.
enum class CursorContext { Code, DoubleString, CharLiteral, Comment };
static CursorContext contextAt(const QTextCursor &cursor) {
    const QTextBlock block=cursor.block();
    bool blockComment=block.previous().isValid() && block.previous().userState()==1;
    QChar quote;
    const QString text=block.text().left(cursor.positionInBlock());
    for(int i=0;i<text.size();++i) {
        const QChar c=text.at(i), next=i+1<text.size()?text.at(i+1):QChar();
        if(blockComment){if(c=='*' && next=='/'){blockComment=false;++i;}continue;}
        if(!quote.isNull()){if(c=='\\'){++i;continue;}if(c==quote)quote=QChar();continue;}
        if(c=='/' && next=='/') return CursorContext::Comment;
        if(c=='/' && next=='*'){blockComment=true;++i;continue;}
        if(c=='"' || c=='\'')quote=c;
    }
    if(blockComment)return CursorContext::Comment;
    if(quote=='"')return CursorContext::DoubleString;
    if(quote=='\'')return CursorContext::CharLiteral;
    return CursorContext::Code;
}

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)), m_highlighter(new SyntaxHighlighter(document())), m_transition(new LineTransitionOverlay(viewport())) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont); font.setPointSize(11); setFont(font);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    auto refreshTransparentSurface=[this] { if(m_transparentBackground) { viewport()->update(); m_lineNumberArea->update(); } };
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, refreshTransparentSurface);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, refreshTransparentSurface);
    updateLineNumberAreaWidth();
    setEditorColors(m_colors);
}

void CodeEditor::setEditorColors(const EditorColors &colors) {
    m_colors = colors;
    QPalette pal = palette();
    pal.setColor(QPalette::Base, m_transparentBackground ? QColor(Qt::transparent) : colors.background);
    pal.setColor(QPalette::Text, colors.foreground);
    pal.setColor(QPalette::Highlight, colors.accent.darker(180));
    pal.setColor(QPalette::HighlightedText, colors.foreground);
    setPalette(pal);
    viewport()->setAutoFillBackground(!m_transparentBackground);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, !m_transparentBackground);
    setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; color: %2; selection-background-color: %3; selection-color: %2; font-family: Consolas; font-size: %4pt; }").arg(m_transparentBackground ? QStringLiteral("transparent") : colors.background.name(), colors.foreground.name(), colors.accent.darker(180).name()).arg(m_fontSize));
    m_highlighter->setColors(colors);
    highlightCurrentLine();
    m_lineNumberArea->update();
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1; int maximum = qMax(1, blockCount());
    while (maximum >= 10) { maximum /= 10; ++digits; }
    return 22 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}
void CodeEditor::updateLineNumberAreaWidth() { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }
void CodeEditor::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy && !m_transparentBackground) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}
void CodeEditor::resizeEvent(QResizeEvent *event) {
    if(m_transition) m_transition->cancel();
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}
void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(m_lineNumberArea);
    if (!m_transparentBackground) painter.fillRect(event->rect(), m_colors.background);
    QTextBlock block = firstVisibleBlock(); int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == textCursor().blockNumber() ? m_colors.accent : m_colors.lineNumber);
            painter.drawText(0, top, m_lineNumberArea->width() - 10, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next(); top = bottom; bottom = top + qRound(blockBoundingRect(block).height()); ++number;
    }
}
void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly() && !m_transparentBackground) {
        QTextEdit::ExtraSelection selection; selection.format.setBackground(m_colors.currentLine);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor(); selection.cursor.clearSelection(); selections.append(selection);
    }
    setExtraSelections(selections); m_lineNumberArea->update();
}

void CodeEditor::setTransparentBackground(bool enabled) {
    if (m_transparentBackground == enabled) return;
    m_transparentBackground = enabled;
    m_transition->cancel();
    setEditorColors(m_colors);
    viewport()->update();
    m_lineNumberArea->update();
}

void CodeEditor::setEditorFontSize(int points) {
    m_fontSize=qBound(9,points,32);
    setEditorColors(m_colors);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' '))*4);
    updateLineNumberAreaWidth();
}

void CodeEditor::setLineAnimationEnabled(bool enabled) {
    m_lineAnimationEnabled=enabled;
    if(!enabled) m_transition->cancel();
}

void CodeEditor::wheelEvent(QWheelEvent *event) {
    m_transition->cancel();
    if(event->modifiers() & Qt::ControlModifier) {
        m_wheelRemainder += event->angleDelta().y();
        const int steps=m_wheelRemainder/120;
        m_wheelRemainder %= 120;
        if(steps) {
            const int next=qBound(9,m_fontSize+steps,32);
            if(next!=m_fontSize) setEditorFontSize(next);
        }
        event->accept(); return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void CodeEditor::mousePressEvent(QMouseEvent *event) {
    m_transition->cancel(); QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::keyPressEvent(QKeyEvent *event) {
    m_transition->cancel();
    if(isReadOnly()) { QPlainTextEdit::keyPressEvent(event); return; }
    if ((event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) && !(event->modifiers() & (Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))) {
        QTextCursor cursor=textCursor();
        const bool animate=m_lineAnimationEnabled && isVisible() && !cursor.hasSelection();
        const QRect oldRect=cursorRect(cursor);
        const int oldVerticalScroll=verticalScrollBar()->value();
        const int oldHorizontalScroll=horizontalScrollBar()->value();
        QImage oldImage;
        if(animate) { oldImage=viewport()->grab().toImage().scaled(viewport()->size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation); oldImage.setDevicePixelRatio(1); }
        if(cursor.hasSelection()) cursor.setPosition(cursor.selectionStart());
        const QString before=cursor.block().text().left(cursor.positionInBlock());
        int n=0;while(n<before.size() && (before.at(n)==' ' || before.at(n)=='\t'))++n;
        const QString base=before.left(n);
        QString indent=base;
        const bool afterOpen=before.trimmed().endsWith('{') && contextAt(cursor)==CursorContext::Code;
        if(afterOpen) indent+=QStringLiteral("    ");
        const QString remainder=cursor.block().text().mid(cursor.positionInBlock());
        const bool betweenBraces=afterOpen && remainder.trimmed().startsWith('}');
        // insertText replaces any selection; keep the original selected cursor for that case.
        if(textCursor().hasSelection()) cursor=textCursor();
        cursor.beginEditBlock();
        cursor.insertText(QStringLiteral("\n")+indent+(betweenBraces?QStringLiteral("\n")+base:QString()));
        QTextCursor tailCursor=cursor;
        if(betweenBraces) cursor.setPosition(cursor.position()-base.size()-1);
        cursor.endEditBlock();setTextCursor(cursor);
        if(animate && !oldImage.isNull()) {
            const QRect tailRect=cursorRect(tailCursor);
            if(tailRect.y()>oldRect.y() && oldRect.y()>=0 && tailRect.y()<viewport()->height() && oldVerticalScroll==verticalScrollBar()->value() && oldHorizontalScroll==horizontalScrollBar()->value()) {
                QImage after=viewport()->grab().toImage().scaled(viewport()->size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation); after.setDevicePixelRatio(1);
                m_transition->start(oldImage,after,oldRect,tailRect,m_colors.currentLine,m_colors.background,m_transparentBackground);
            }
        }
        return;
    }
    if (event->key()==Qt::Key_Tab && !event->modifiers() && !textCursor().hasSelection()) {
        textCursor().insertText(QStringLiteral("    ")); return;
    }
    const Qt::KeyboardModifiers ignored=Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier;
    if(!(event->modifiers() & ignored)) {
        QTextCursor c=textCursor();
        const CursorContext context=contextAt(c);
        const QChar next=document()->characterAt(c.position());
        const QChar prev=c.position()>0?document()->characterAt(c.position()-1):QChar();
        if(event->key()==Qt::Key_Backspace && !c.hasSelection() && context==CursorContext::Code) {
            const bool emptyPair=(prev=='('&&next==')') || (prev=='['&&next==']') || (prev=='{'&&next=='}');
            if(emptyPair){c.beginEditBlock();c.deleteChar();c.deletePreviousChar();c.endEditBlock();setTextCursor(c);return;}
        }
        const QString typed=event->text();
        if(typed.size()==1) {
            const QChar ch=typed.at(0);
            const bool quote=(ch=='"');
            const bool closing=(ch==')'||ch==']'||ch=='}');
            // Existing closers are skipped only when typing an actual closer.
            if(!c.hasSelection() && next==ch && ((closing && context==CursorContext::Code) || (quote && (context==CursorContext::Code || context==CursorContext::DoubleString))) && (prev!='\\' || !quote)) {
                c.movePosition(QTextCursor::NextCharacter);setTextCursor(c);return;
            }
            QChar other;
            if(ch=='(')other=')';else if(ch=='[')other=']';else if(ch=='{')other='}';else if(quote)other=ch;
            if(!other.isNull() && context==CursorContext::Code && (!quote || prev!='\\')) {
                if(c.hasSelection()) {
                    const int begin=c.selectionStart(), end=c.selectionEnd();
                    c.beginEditBlock();c.setPosition(end);c.insertText(QString(other));c.setPosition(begin);c.insertText(QString(ch));c.setPosition(begin+1);c.setPosition(end+1,QTextCursor::KeepAnchor);c.endEditBlock();setTextCursor(c);
                } else {
                    c.insertText(QString(ch)+other);c.movePosition(QTextCursor::PreviousCharacter);setTextCursor(c);
                }
                return;
            }
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}
