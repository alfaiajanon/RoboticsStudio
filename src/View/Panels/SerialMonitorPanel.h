#pragma once

#include <QHash>
#include <QStringDecoder>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTimer;
class SerialHub;


/*
 * The Serial Monitor: one tab per board that runs, as the Arduino IDE's
 * monitor: what the board's Serial prints, and a line to send to it. At the
 * Real level the monitor has its own baud rate (saved per board in the
 * project); text sent at another rate shows as garbage, as on the bench. The
 * ideal board (Prototype) always matches.
 *
 * Reads the SimulationManager's SerialHub on a timer; the tabs follow the
 * boards of the last Play. Input is enabled while a run is going.
 */
class SerialMonitorPanel : public QWidget {
    Q_OBJECT

    struct Page {
        int uid = -1;
        QString name;
        bool ideal = true;
        QWidget* widget = nullptr;
        QPlainTextEdit* text = nullptr;
        QLineEdit* input = nullptr;
        QPushButton* send = nullptr;
        QComboBox* ending = nullptr;
        QComboBox* baud = nullptr;
        QCheckBox* timestamps = nullptr;
        QCheckBox* autoscroll = nullptr;
        QStringDecoder decoder{QStringDecoder::Utf8};
        bool atLineStart = true;
        bool warnedBaud = false;
    };

    QStackedWidget* stack = nullptr;
    QLabel* placeholder = nullptr;
    QTabWidget* tabs = nullptr;
    QList<Page*> pages;
    QTimer* timer = nullptr;
    int seenGeneration = -1;

    SerialHub* hub() const;
    void rebuildTabs();
    void poll();
    void append(Page* page, double time, double baud, const QByteArray& bytes);
    void sendLine(Page* page);

    public:
        explicit SerialMonitorPanel(QWidget* parent = nullptr);
        ~SerialMonitorPanel() override;
};
