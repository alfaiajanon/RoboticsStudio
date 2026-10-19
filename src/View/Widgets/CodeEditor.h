#pragma once

#include <QPlainTextEdit>
#include <QWidget>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QRegularExpression>



class SyntaxHighlighter;
class LineNumberArea;


enum class CodeLanguage { JavaScript, Cpp };

// A message the compiler attached to a line (1-based).
struct LineMarker {
    int line = 0;
    bool error = true;      // false: a warning
    QString message;
};




// The main text editor widget
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

    public:
        CodeEditor(QWidget *parent = nullptr);

        void lineNumberAreaPaintEvent(QPaintEvent *event);
        int lineNumberAreaWidth();

        void setLanguage(CodeLanguage language);

        // Dots in the gutter (tooltip: the messages) and a tinted line. Each
        // marker follows its line while the text around it is edited.
        void setLineMarkers(const QList<LineMarker>& markers);
        void clearLineMarkers();
        QString markerTextAt(int y) const;   // gutter y -> messages of that line
        void goToLine(int line);

    protected:
        void resizeEvent(QResizeEvent *event) override;

    private slots:
        void updateLineNumberAreaWidth(int newBlockCount);
        void highlightCurrentLine();
        void updateLineNumberArea(const QRect &rect, int dy);

    private:
        struct Marker {
            QTextCursor anchor;     // at the start of the line
            bool error = true;
            QStringList messages;
        };

        QWidget *lineNumberArea;
        SyntaxHighlighter *highlighter;
        QList<Marker> markers;

        const Marker* markerForBlock(int blockNumber) const;
};








// A companion widget that paints the line numbers for the CodeEditor
class LineNumberArea : public QWidget {
    public:
        LineNumberArea(CodeEditor *editor) : QWidget(editor), codeEditor(editor) {}
        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent *event) override;
        bool event(QEvent *event) override;   // tooltips for line markers

    private:
        CodeEditor *codeEditor;
};







// Rule-based highlighting for the project's scripts: C++ (firmware sketches,
// with the Arduino API's names) or JavaScript (stage scripts).
class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

    public:
        explicit SyntaxHighlighter(QTextDocument* parent = nullptr);
        void setLanguage(CodeLanguage language);

    protected:
        void highlightBlock(const QString& text) override;

    private:
        struct HighlightingRule {
            QRegularExpression pattern;
            QTextCharFormat format;
        };
        QList<HighlightingRule> highlightingRules;
        CodeLanguage language = CodeLanguage::JavaScript;

        QTextCharFormat keywordFormat;
        QTextCharFormat typeFormat;
        QTextCharFormat builtinFormat;
        QTextCharFormat preprocessorFormat;
        QTextCharFormat singleLineCommentFormat;
        QTextCharFormat multiLineCommentFormat;
        QTextCharFormat quotationFormat;
        QTextCharFormat functionFormat;
        QTextCharFormat numberFormat;

        void buildRules();
};
