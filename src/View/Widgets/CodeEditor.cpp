#include "CodeEditor.h"
#include <QPainter>
#include <QTextBlock>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QToolTip>
#include "View/Theme.h"



CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    lineNumberArea = new LineNumberArea(this);
    
    // Attach the syntax highlighter
    highlighter = new SyntaxHighlighter(this->document());

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
    // Lines the compiler complained about
    for (const Marker& m : markers) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(m.error ? QColor(229, 72, 77, 38) : QColor(245, 165, 36, 30));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = m.anchor;
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }
    setExtraSelections(extraSelections);
}



static const QColor kErrorColor{0xe5, 0x48, 0x4d};
static const QColor kWarningColor{0xf5, 0xa5, 0x24};

void CodeEditor::setLanguage(CodeLanguage language) {
    highlighter->setLanguage(language);
}

void CodeEditor::setLineMarkers(const QList<LineMarker>& list) {
    markers.clear();
    for (const LineMarker& lm : list) {
        QTextBlock block = document()->findBlockByNumber(lm.line - 1);
        if (!block.isValid()) continue;
        Marker* existing = nullptr;
        for (Marker& m : markers) if (m.anchor.blockNumber() == block.blockNumber()) existing = &m;
        if (!existing) {
            markers.append({QTextCursor(block), lm.error, {}});
            existing = &markers.last();
        }
        existing->error = existing->error || lm.error;
        existing->messages << (lm.error ? "error: " : "warning: ") + lm.message;
    }
    highlightCurrentLine();
    lineNumberArea->update();
}

void CodeEditor::clearLineMarkers() {
    if (markers.isEmpty()) return;
    markers.clear();
    highlightCurrentLine();
    lineNumberArea->update();
}

const CodeEditor::Marker* CodeEditor::markerForBlock(int blockNumber) const {
    for (const Marker& m : markers) if (m.anchor.blockNumber() == blockNumber) return &m;
    return nullptr;
}

QString CodeEditor::markerTextAt(int y) const {
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid()) {
        const int bottom = top + qRound(blockBoundingRect(block).height());
        if (y >= top && y < bottom) {
            const Marker* m = markerForBlock(block.blockNumber());
            return m ? m->messages.join('\n') : QString();
        }
        block = block.next();
        top = bottom;
    }
    return QString();
}

void CodeEditor::goToLine(int line) {
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    setTextCursor(QTextCursor(block));
    centerCursor();
    setFocus();
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
            if (const Marker* m = markerForBlock(blockNumber)) {
                const int d = qMax(6, fontMetrics().height() / 2);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(Qt::NoPen);
                painter.setBrush(m->error ? kErrorColor : kWarningColor);
                painter.drawEllipse(QRectF(5, top + (fontMetrics().height() - d) / 2.0, d, d));
                painter.setRenderHint(QPainter::Antialiasing, false);
            }
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

bool LineNumberArea::event(QEvent *event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const QString text = codeEditor->markerTextAt(help->pos().y());
        if (text.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}









SyntaxHighlighter::SyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    keywordFormat.setForeground(Theme::accent);
    keywordFormat.setFontWeight(QFont::DemiBold);
    typeFormat.setForeground(QColor("#4ec9b0"));                 // teal: types
    builtinFormat.setForeground(QColor("#c9a7e8"));              // lilac: Arduino API, constants
    preprocessorFormat.setForeground(QColor("#c792b8"));
    numberFormat.setForeground(QColor("#ddbb7c"));               // warm amber, mixed toward text
    quotationFormat.setForeground(QColor("#dcb163"));            // amber
    functionFormat.setForeground(QColor("#bfd1e8"));             // text tinted toward the accent
    singleLineCommentFormat.setForeground(QColor("#669d7a"));    // muted green, italic
    singleLineCommentFormat.setFontItalic(true);
    multiLineCommentFormat = singleLineCommentFormat;
    buildRules();
}

void SyntaxHighlighter::setLanguage(CodeLanguage lang) {
    if (lang == language) return;
    language = lang;
    buildRules();
    rehighlight();
}

// Later rules win where matches overlap: strings and comments come last.
void SyntaxHighlighter::buildRules() {
    highlightingRules.clear();
    auto words = [this](const QStringList& list, const QTextCharFormat& format) {
        highlightingRules.append({QRegularExpression("\\b(" + list.join('|') + ")\\b"), format});
    };

    if (language == CodeLanguage::JavaScript) {
        words({"function", "let", "var", "const", "if", "else", "for", "while", "do", "return", "new",
               "class", "this", "true", "false", "null", "undefined", "break", "continue", "switch", "case"},
              keywordFormat);
    } else {
        words({"if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case", "default",
               "class", "struct", "enum", "union", "public", "private", "protected", "virtual", "override",
               "const", "constexpr", "static", "volatile", "inline", "extern", "template", "typename",
               "namespace", "using", "new", "delete", "this", "true", "false", "nullptr", "sizeof", "auto",
               "goto", "typedef", "operator"},
              keywordFormat);
        words({"void", "bool", "boolean", "char", "short", "int", "long", "float", "double", "unsigned",
               "signed", "byte", "word", "size_t", "String", "Servo",
               "u?int(8|16|32|64)_t"},
              typeFormat);
        words({"HIGH", "LOW", "INPUT", "OUTPUT", "INPUT_PULLUP", "PI", "HALF_PI", "TWO_PI", "DEG_TO_RAD",
               "RAD_TO_DEG", "DEC", "HEX", "OCT", "BIN", "Serial", "setup", "loop",
               "pinMode", "digitalWrite", "digitalRead", "analogWrite", "analogRead", "millis", "micros",
               "delay", "delayMicroseconds", "map", "constrain", "random", "randomSeed",
               "[A-Z][A-Z0-9]*_[A-Z0-9_]+"},   // ALL_CAPS constants: pin names, MAX_PULSE_WIDTH
              builtinFormat);
    }

    highlightingRules.append({QRegularExpression(QStringLiteral("\\b[0-9]+(\\.[0-9]+)?[fFuUlL]*\\b|\\b0x[0-9A-Fa-f]+\\b")), numberFormat});
    highlightingRules.append({QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_]+(?=\\()")), functionFormat});
    if (language == CodeLanguage::Cpp)
        highlightingRules.append({QRegularExpression(QStringLiteral("^\\s*#\\s*[a-z]+.*$")), preprocessorFormat});
    highlightingRules.append({QRegularExpression(QStringLiteral("\"(\\\\.|[^\"\\\\])*\"|'(\\\\.|[^'\\\\])*'")), quotationFormat});
    highlightingRules.append({QRegularExpression(QStringLiteral("//[^\n]*")), singleLineCommentFormat});
}

void SyntaxHighlighter::highlightBlock(const QString& text) {
    for (const HighlightingRule& rule : std::as_const(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Block comments, which may span lines (block state 1 = inside one).
    static const QRegularExpression startExpr(QStringLiteral("/\\*"));
    static const QRegularExpression endExpr(QStringLiteral("\\*/"));
    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : int(text.indexOf(startExpr));
    while (start >= 0) {
        const QRegularExpressionMatch end = endExpr.match(text, start);
        int length;
        if (!end.hasMatch()) {
            setCurrentBlockState(1);
            length = int(text.length()) - start;
        } else {
            length = int(end.capturedEnd()) - start;
        }
        setFormat(start, length, multiLineCommentFormat);
        start = int(text.indexOf(startExpr, start + length));
    }
}
