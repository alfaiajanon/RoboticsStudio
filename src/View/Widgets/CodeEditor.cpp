#include "CodeEditor.h"
#include "CodeEditor.h"
#include <QPainter>
#include <QTextBlock>
#include <QFontDatabase>
#include "View/Theme.h"



CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    lineNumberArea = new LineNumberArea(this);
    
    // Attach the syntax highlighter
    highlighter = new JSHighlighter(this->document());

    // Setup dark theme fonts
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSizeF(10.5);
    setFont(monoFont);

    // The font is repeated here: the global dark.qss font-family would otherwise win over setFont
    setStyleSheet(QString(R"(
        QPlainTextEdit {
            font-family: "%1";
            font-size: 10.5pt;
            background-color: #202124;
            color: #e6e7e9;
            border: none;
            border-radius: 0px;
            padding: 0px;
            selection-background-color: #45474c;
        }
    )").arg(monoFont.family()));
    ensurePolished();
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();

    // Lets the last lines scroll up past the floating HUD at the bottom-left
    setCenterOnScroll(true);
}

int CodeEditor::lineNumberAreaWidth() {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    // Calculate width: padding + (digits * width per digit)
    int space = 23 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) {
        lineNumberArea->scroll(0, dy);
    } else {
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth(0);
    }
}

void CodeEditor::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        // Subtle highlight for the line the cursor is currently on (surface + 4.5% text)
        QColor lineColor = QColor("#28292c");
        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }
    setExtraSelections(extraSelections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(lineNumberArea);
    // Gutter: same surface as the text, separated by a 1px line
    painter.fillRect(event->rect(), Theme::surface);
    painter.setPen(Theme::border);
    painter.drawLine(lineNumberArea->width() - 1, event->rect().top(),
                     lineNumberArea->width() - 1, event->rect().bottom());
    const int currentBlock = textCursor().blockNumber();

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(blockNumber == currentBlock ? Theme::textMuted : Theme::textFaint);
            painter.drawText(0, top, lineNumberArea->width() - 8, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

QSize LineNumberArea::sizeHint() const {
    return QSize(codeEditor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
    codeEditor->lineNumberAreaPaintEvent(event);
}









JSHighlighter::JSHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    
    HighlightingRule rule;

    // 1. Keywords (accent)
    keywordFormat.setForeground(Theme::accent);
    keywordFormat.setFontWeight(QFont::DemiBold);
    const QString keywordPatterns[] = {
        QStringLiteral("\\bfunction\\b"), QStringLiteral("\\blet\\b"), 
        QStringLiteral("\\bvar\\b"), QStringLiteral("\\bconst\\b"),
        QStringLiteral("\\bif\\b"), QStringLiteral("\\belse\\b"), 
        QStringLiteral("\\bfor\\b"), QStringLiteral("\\bwhile\\b"),
        QStringLiteral("\\breturn\\b"), QStringLiteral("\\bnew\\b"), 
        QStringLiteral("\\bclass\\b"), QStringLiteral("\\bthis\\b"),
        QStringLiteral("\\btrue\\b"), QStringLiteral("\\bfalse\\b")
    };
    for (const QString& pattern : keywordPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    // 2. Numbers (warm amber, mixed toward text)
    numberFormat.setForeground(QColor("#ddbb7c"));
    rule.pattern = QRegularExpression(QStringLiteral("\\b[0-9]+(\\.[0-9]+)?\\b"));
    rule.format = numberFormat;
    highlightingRules.append(rule);

    // 3. Strings (amber)
    quotationFormat.setForeground(QColor("#dcb163"));
    rule.pattern = QRegularExpression(QStringLiteral("\".*?\"|'.*?'"));
    rule.format = quotationFormat;
    highlightingRules.append(rule);

    // 4. Functions (text tinted toward the accent)
    functionFormat.setForeground(QColor("#bfd1e8"));
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_]+(?=\\()"));
    rule.format = functionFormat;
    highlightingRules.append(rule);

    // 5. Single Line Comments (muted green, italic)
    singleLineCommentFormat.setForeground(QColor("#669d7a"));
    singleLineCommentFormat.setFontItalic(true);
    rule.pattern = QRegularExpression(QStringLiteral("//[^\n]*"));
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);
}

void JSHighlighter::highlightBlock(const QString& text) {
    for (const HighlightingRule& rule : std::as_const(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}