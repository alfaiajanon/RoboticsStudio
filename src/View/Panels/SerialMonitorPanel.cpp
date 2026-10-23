#include "SerialMonitorPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include "Application/Application.h"
#include "Document/Project.h"
#include "Simulation/SimulationManager.h"
#include "Simulation/Controller/SerialHub.h"
#include "Utils/Log.h"

static const int kBauds[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 74880, 115200,
                             230400, 250000, 500000, 1000000, 2000000};

// A receiver more than 2.5 % off the sender's rate samples the wrong bits.
static bool baudMatches(double sender, double receiver) {
    return sender <= 0.0 || receiver <= 0.0 || std::fabs(sender / receiver - 1.0) <= 0.025;
}

// What a monitor at the wrong baud rate shows for a byte: mostly odd
// characters, some of them the IDE's replacement mark.
static QChar garbled(uint8_t value, quint64 index) {
    quint64 x = (quint64(value) << 32) ^ (index * 0x9e3779b97f4a7c15ULL);
    x ^= x >> 31; x *= 0xbf58476d1ce4e5b9ULL; x ^= x >> 29;
    const uint8_t c = uint8_t(x);
    if (c < 0xA0) return QChar(0x2E2E);   // ⸮
    return QChar(c);                       // a Latin-1 letter or sign
}



SerialMonitorPanel::SerialMonitorPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    stack = new QStackedWidget(this);
    placeholder = new QLabel("Press Play: each board's Serial appears here, with a line to send to it.", this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    placeholder->setStyleSheet("QLabel { color: #9aa0a6; }");
    tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    stack->addWidget(placeholder);
    stack->addWidget(tabs);
    layout->addWidget(stack);

    timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, &SerialMonitorPanel::poll);
    timer->start();
}



SerialMonitorPanel::~SerialMonitorPanel() {
    qDeleteAll(pages);
}



SerialHub* SerialMonitorPanel::hub() const {
    Application* app = Application::getInstance();
    SimulationManager* sim = app ? app->getSimulationManager() : nullptr;
    return sim ? sim->getSerialHub() : nullptr;
}



// One tab per board of the run that just started (the hub's list).
void SerialMonitorPanel::rebuildTabs() {
    SerialHub* h = hub();
    if (!h) return;
    const QList<SerialHub::BoardInfo> boards = h->boards();
    while (tabs->count()) {
        QWidget* w = tabs->widget(0);
        tabs->removeTab(0);
        w->deleteLater();
    }
    qDeleteAll(pages);
    pages.clear();

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const SerialHub::BoardInfo& b : boards) {
        Page* page = new Page;
        page->uid = b.uid;
        page->name = b.name;
        page->ideal = b.ideal;
        page->widget = new QWidget();
        QVBoxLayout* v = new QVBoxLayout(page->widget);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        page->text = new QPlainTextEdit(page->widget);
        page->text->setReadOnly(true);
        page->text->setLineWrapMode(QPlainTextEdit::NoWrap);
        page->text->setMaximumBlockCount(5000);
        page->text->setFrameShape(QFrame::NoFrame);
        page->text->setStyleSheet(QString("QPlainTextEdit { font-family: \"%1\"; font-size: 10.5pt; background-color: #202124;"
                                          " border: none; border-radius: 0px; padding: 6px 8px; }").arg(fixed.family()));
        v->addWidget(page->text);

        QFrame* bar = new QFrame(page->widget);
        bar->setStyleSheet("QFrame { background-color: #27282b; border-top: 1px solid #101113; } "
                           "QCheckBox { background-color: transparent; border: none; } ");
        QHBoxLayout* row = new QHBoxLayout(bar);
        row->setContentsMargins(5, 5, 5, 5);
        page->input = new QLineEdit(bar);
        page->input->setPlaceholderText(QString("Message to %1's Serial (Enter to send)").arg(b.ideal ? QString("the board") : b.name));
        page->send = new QPushButton("Send", bar);
        page->ending = new QComboBox(bar);
        page->ending->addItem("No line ending", QByteArray());
        page->ending->addItem("Newline", QByteArray("\n"));
        page->ending->addItem("Carriage return", QByteArray("\r"));
        page->ending->addItem("Both NL & CR", QByteArray("\r\n"));
        page->ending->setCurrentIndex(1);
        page->ending->setToolTip("Appended to each line sent, as in the Arduino IDE");
        row->addWidget(page->input, 1);
        row->addWidget(page->send);
        row->addWidget(page->ending);
        if (!b.ideal) {
            page->baud = new QComboBox(bar);
            for (int baud : kBauds) page->baud->addItem(QString("%1 baud").arg(baud), baud);
            const int current = int(h->monitorBaud(b.uid));
            int idx = page->baud->findData(current);
            if (idx < 0) { page->baud->addItem(QString("%1 baud").arg(current), current); idx = page->baud->count() - 1; }
            page->baud->setCurrentIndex(idx);
            page->baud->setToolTip("The monitor's baud rate. It must match the sketch's Serial.begin(); "
                                   "otherwise both directions read garbage, as on a real board");
            row->addWidget(page->baud);
            connect(page->baud, &QComboBox::currentIndexChanged, this, [this, page]() {
                const int baud = page->baud->currentData().toInt();
                if (SerialHub* hb = hub()) hb->setMonitorBaud(page->uid, baud);
                if (Project* p = Application::getInstance()->getProject()) p->setSerialMonitorBaud(page->uid, baud);
                page->warnedBaud = false;
            });
        }
        page->timestamps = new QCheckBox("Timestamps", bar);
        page->timestamps->setToolTip("Prefix each line with the sim time it started at");
        page->autoscroll = new QCheckBox("Autoscroll", bar);
        page->autoscroll->setChecked(true);
        QPushButton* clear = new QPushButton("Clear", bar);
        row->addWidget(page->timestamps);
        row->addWidget(page->autoscroll);
        row->addWidget(clear);
        v->addWidget(bar);

        connect(page->input, &QLineEdit::returnPressed, this, [this, page]() { sendLine(page); });
        connect(page->send, &QPushButton::clicked, this, [this, page]() { sendLine(page); });
        connect(clear, &QPushButton::clicked, page->text, &QPlainTextEdit::clear);

        pages.append(page);
        tabs->addTab(page->widget, b.ideal ? QString("Ideal board") : b.name);
    }
    stack->setCurrentWidget(pages.isEmpty() ? static_cast<QWidget*>(placeholder) : tabs);
}



void SerialMonitorPanel::poll() {
    SerialHub* h = hub();
    if (!h) return;
    if (h->generation() != seenGeneration) {
        seenGeneration = h->generation();
        rebuildTabs();
    }
    SimulationManager* sim = Application::getInstance()->getSimulationManager();
    const bool running = sim && sim->getState() != SimulationState::EDITING;
    for (Page* page : pages) {
        page->input->setEnabled(running);
        page->send->setEnabled(running);
        for (const SerialHub::Chunk& c : h->takeOutput(page->uid)) append(page, c.time, c.baud, c.bytes);
    }
}



void SerialMonitorPanel::append(Page* page, double time, double baud, const QByteArray& bytes) {
    const double monitor = page->baud ? page->baud->currentData().toDouble() : 0.0;
    const bool garbage = !page->ideal && !baudMatches(baud, monitor);
    if (garbage && !page->warnedBaud) {
        page->warnedBaud = true;
        Log::warning(QString("Serial Monitor: %1 sends at %2 baud, the monitor is set to %3 baud: it shows garbage. "
                             "Set the monitor to %2 (or change Serial.begin())").arg(page->name).arg(baud).arg(monitor));
    }

    QString text;
    static quint64 counter = 0;
    if (garbage) {
        for (char c : bytes) text += c == '\n' ? QChar('\n') : garbled(uint8_t(c), counter++);
    } else {
        text = page->decoder.decode(bytes);
    }
    text.remove('\r');
    if (page->timestamps->isChecked()) {
        QString stamped;
        const QString stamp = QString("[%1 s] ").arg(time, 0, 'f', 3);
        for (QChar c : text) {
            if (page->atLineStart) stamped += stamp;
            stamped += c;
            page->atLineStart = c == '\n';
        }
        text = stamped;
    } else if (!text.isEmpty()) {
        page->atLineStart = text.endsWith('\n');
    }

    QScrollBar* bar = page->text->verticalScrollBar();
    const int keep = bar->value();
    QTextCursor cursor(page->text->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (page->autoscroll->isChecked()) bar->setValue(bar->maximum());
    else bar->setValue(keep);
}



void SerialMonitorPanel::sendLine(Page* page) {
    SerialHub* h = hub();
    if (!h || !page->input->isEnabled()) return;
    const QByteArray bytes = page->input->text().toUtf8() + page->ending->currentData().toByteArray();
    if (bytes.isEmpty()) return;
    h->send(page->uid, bytes);
    page->input->clear();
}
