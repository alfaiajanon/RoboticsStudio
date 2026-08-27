#pragma once

#include <QPlainTextEdit>
#include <QWidget>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>



class JSHighlighter;
class LineNumberArea;




// The main text editor widget
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

    public:
        CodeEditor(QWidget *parent = nullptr);
        
        void lineNumberAreaPaintEvent(QPaintEvent *event);
        int lineNumberAreaWidth();

    protected:
        void resizeEvent(QResizeEvent *event) override;

    private slots:
        void updateLineNumberAreaWidth(int newBlockCount);
        void highlightCurrentLine();
        void updateLineNumberArea(const QRect &rect, int dy);

    private:
        QWidget *lineNumberArea;
        JSHighlighter *highlighter;
};








// A companion widget that paints the line numbers for the CodeEditor
class LineNumberArea : public QWidget {
    public:
        LineNumberArea(CodeEditor *editor) : QWidget(editor), codeEditor(editor) {}
        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent *event) override;

    private:
        CodeEditor *codeEditor;
};







class JSHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

    public:
        explicit JSHighlighter(QTextDocument* parent = nullptr);

    protected:
        void highlightBlock(const QString& text) override;

    private:
        struct HighlightingRule {
            QRegularExpression pattern;
            QTextCharFormat format;
        };
        QList<HighlightingRule> highlightingRules;

        QTextCharFormat keywordFormat;
        QTextCharFormat classFormat;
        QTextCharFormat singleLineCommentFormat;
        QTextCharFormat multiLineCommentFormat;
        QTextCharFormat quotationFormat;
        QTextCharFormat functionFormat;
        QTextCharFormat numberFormat;
};