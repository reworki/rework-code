#pragma once

#include <QColor>
#include <QPlainTextEdit>

struct EditorColors {
    QColor background = QColor("#101722");
    QColor foreground = QColor("#d6e2ef");
    QColor lineNumber = QColor("#687c90");
    QColor currentLine = QColor("#1b2b3b");
    QColor keyword = QColor("#c69cff");
    QColor stringLiteral = QColor("#a9dca1");
    QColor comment = QColor("#718b92");
    QColor number = QColor("#f0bd81");
    QColor function = QColor("#91c8f5");
    QColor variable = QColor("#d6e2ef");
    QColor accent = QColor("#65d9e8");
};

class QPaintEvent;
class QResizeEvent;
class QKeyEvent;
class QWheelEvent;
class LineTransitionOverlay;
class SyntaxHighlighter;
class LineNumberArea;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);
    void setEditorColors(const EditorColors &colors);
    EditorColors editorColors() const { return m_colors; }
    void setTransparentBackground(bool enabled);
    bool transparentBackground() const { return m_transparentBackground; }
    void setEditorFontSize(int points);
    int editorFontSize() const { return m_fontSize; }
    void setLineAnimationEnabled(bool enabled);
    bool lineAnimationEnabled() const { return m_lineAnimationEnabled; }
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();
    LineNumberArea *m_lineNumberArea;
    SyntaxHighlighter *m_highlighter;
    EditorColors m_colors;
    LineTransitionOverlay *m_transition;
    bool m_lineAnimationEnabled = true;
    bool m_transparentBackground = true;
    int m_wheelRemainder = 0;
    int m_fontSize = 11;
};
