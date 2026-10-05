#pragma once

#include "Emulator.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Utils/Log.h"
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

/*
 * Generic RGB framebuffer display, Adafruit-GFX style: JS draws into a back
 * buffer, display() sends it to the panel, the panel shows it at its refresh
 * rate.
 *
 *   back buffer   MCU thread only (all drawing calls), so no lock.
 *   display()     copies the back buffer into `sent` (mutex) and blocks for
 *                 transfer_ms, like pushing the buffer over I2C/SPI.
 *   update()      Throttled at refresh_hz (physics thread): publishes the
 *                 newest sent buffer into the display signal's ImageIOValue.
 *
 * Drives the first display-kind input signal of the component.
 *
 * Parameters (component .rsdef -> emulator.parameters), read in init():
 *   refresh_hz   panel refresh rate, sim time   (default 30)
 *   transfer_ms  time display() blocks the MCU  (default 0, capped at 200)
 *
 * Colours from JS: a number 0xRRGGBB, or any string QColor accepts
 * ("#ff8800", "red").
 */
class DisplayEmulatorCpp : public Emulator {
    Q_OBJECT

    private:
        QString signal;
        double refreshHz = 30.0;
        int transferMs = 0;

        QImage back;   // MCU thread only

        std::mutex sentMutex;
        std::shared_ptr<const QByteArray> sent;   // last buffer display() sent
        quint64 sentSeq = 0;                      // guarded by sentMutex
        quint64 publishedSeq = 0;                 // physics thread only

        static QColor toColor(const QVariant& v) {
            if (v.typeId() == QMetaType::QString) return QColor(v.toString());
            return QColor::fromRgb(QRgb(v.toUInt() | 0xff000000u));
        }

        bool ready() const { return !back.isNull(); }

    public:
        using Emulator::Emulator;

        void init() override {
            signal.clear();
            back = QImage();
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            if (!bp) return;

            const auto& params = bp->emulatorDef.parameters;
            refreshHz = params.value("refresh_hz", 30.0).toDouble();
            if (refreshHz <= 0.0) refreshHz = 30.0;
            transferMs = std::clamp(params.value("transfer_ms", 0).toInt(), 0, 200);

            for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
                if (it.value().target.kind != "display") continue;
                if (!signal.isEmpty()) {
                    Log::warning(QString("DisplayEmulator: component %1: only the first display signal ('%2') is driven; '%3' ignored")
                                    .arg(component->uid).arg(signal, it.key()));
                    continue;
                }
                signal = it.key();
            }
            if (signal.isEmpty()) return;

            ImageIOValue img = component->getImageValue(signal);
            if (img.width <= 0 || img.height <= 0) {
                Log::error(QString("DisplayEmulator: component %1: display signal '%2' has no resolution; disabled")
                                .arg(component->uid).arg(signal));
                signal.clear();
                return;
            }
            back = QImage(img.width, img.height, QImage::Format_RGB888);
            reset();
        }

        Schedule schedule() const override {
            return {Cadence::Throttled, 1.0 / refreshHz};
        }

        void update() override {
            if (signal.isEmpty()) return;
            std::shared_ptr<const QByteArray> frame;
            quint64 seq;
            {
                std::lock_guard<std::mutex> lock(sentMutex);
                frame = sent;
                seq = sentSeq;
            }
            if (!frame || seq == publishedSeq) return;
            publishedSeq = seq;

            ImageIOValue img = component->getImageValue(signal);
            img.format = ImageFormat::RGB888;
            img.time = simTime;
            img.seq = seq;
            img.pixels = frame;
            component->setImageValue(signal, img);
        }

        // Blank panel: back buffer, sent buffer and published frame.
        void reset() override {
            if (!back.isNull()) back.fill(Qt::black);
            {
                std::lock_guard<std::mutex> lock(sentMutex);
                sent.reset();
                sentSeq = 0;
            }
            publishedSeq = 0;
            if (!signal.isEmpty()) {
                ImageIOValue img = component->getImageValue(signal);
                img.pixels.reset();
                img.seq = 0;
                img.time = 0.0;
                component->setImageValue(signal, img);
            }
        }



        // ---- API exposed to JS (drawing goes to the back buffer; nothing is
        // ---- visible until display()).

        Q_INVOKABLE int width() const  { return back.width(); }
        Q_INVOKABLE int height() const { return back.height(); }

        Q_INVOKABLE void clear() { fill(0); }

        Q_INVOKABLE void fill(const QVariant& color) {
            if (ready()) back.fill(toColor(color));
        }

        Q_INVOKABLE void drawPixel(int x, int y, const QVariant& color) {
            if (ready() && back.valid(x, y)) back.setPixelColor(x, y, toColor(color));
        }

        Q_INVOKABLE void drawLine(int x0, int y0, int x1, int y1, const QVariant& color) {
            if (!ready()) return;
            QPainter p(&back);
            p.setPen(toColor(color));
            p.drawLine(x0, y0, x1, y1);
        }

        Q_INVOKABLE void drawRect(int x, int y, int w, int h, const QVariant& color) {
            if (!ready() || w <= 0 || h <= 0) return;
            QPainter p(&back);
            p.setPen(toColor(color));
            p.drawRect(x, y, w - 1, h - 1);
        }

        Q_INVOKABLE void fillRect(int x, int y, int w, int h, const QVariant& color) {
            if (!ready()) return;
            QPainter p(&back);
            p.fillRect(x, y, w, h, toColor(color));
        }

        Q_INVOKABLE void drawCircle(int x, int y, int r, const QVariant& color) {
            if (!ready()) return;
            QPainter p(&back);
            p.setPen(toColor(color));
            p.drawEllipse(QPoint(x, y), r, r);
        }

        Q_INVOKABLE void fillCircle(int x, int y, int r, const QVariant& color) {
            if (!ready()) return;
            QPainter p(&back);
            p.setPen(Qt::NoPen);
            p.setBrush(toColor(color));
            p.drawEllipse(QPoint(x, y), r, r);
        }

        // (x, y) is the top-left of the text; size in pixels.
        Q_INVOKABLE void drawText(int x, int y, const QString& text, const QVariant& color, int size = 8) {
            if (!ready()) return;
            QPainter p(&back);
            QFont font;
            font.setPixelSize(std::max(1, size));
            font.setStyleStrategy(QFont::NoAntialias);
            p.setFont(font);
            p.setPen(toColor(color));
            p.drawText(QRect(x, y, back.width() - x, back.height() - y), Qt::AlignLeft | Qt::AlignTop, text);
        }

        // Blit a grayscale (1 byte/pixel) image, e.g. a camera's toGray(),
        // with its top-left at (x, y). Pixels outside the panel are clipped.
        Q_INVOKABLE void drawGray(const QByteArray& data, int w, int h, int x = 0, int y = 0) {
            if (!ready() || w <= 0 || h <= 0 || data.size() < qsizetype(w) * h) return;
            const unsigned char* src = reinterpret_cast<const unsigned char*>(data.constData());
            for (int yy = std::max(0, -y); yy < h && y + yy < back.height(); ++yy) {
                uchar* row = back.scanLine(y + yy);
                for (int xx = std::max(0, -x); xx < w && x + xx < back.width(); ++xx) {
                    uchar v = src[yy * w + xx];
                    uchar* px = row + (x + xx) * 3;
                    px[0] = px[1] = px[2] = v;
                }
            }
        }

        // Blit an RGB888 (3 bytes/pixel) image, e.g. a camera frame's data.
        Q_INVOKABLE void drawRGB(const QByteArray& data, int w, int h, int x = 0, int y = 0) {
            if (!ready() || w <= 0 || h <= 0 || data.size() < qsizetype(w) * h * 3) return;
            const unsigned char* src = reinterpret_cast<const unsigned char*>(data.constData());
            for (int yy = std::max(0, -y); yy < h && y + yy < back.height(); ++yy) {
                uchar* row = back.scanLine(y + yy);
                const int x0 = std::max(0, -x);
                const int x1 = std::min(w, back.width() - x);
                if (x1 > x0) std::copy_n(src + (yy * w + x0) * 3, (x1 - x0) * 3, row + (x + x0) * 3);
            }
        }

        // Send the back buffer to the panel. Blocks for transfer_ms.
        Q_INVOKABLE void display() {
            if (!ready()) return;
            auto frame = std::make_shared<QByteArray>(back.width() * back.height() * 3, '\0');
            const int rowBytes = back.width() * 3;
            for (int y = 0; y < back.height(); ++y) {
                std::copy_n(back.constScanLine(y), rowBytes, frame->data() + y * rowBytes);
            }
            {
                std::lock_guard<std::mutex> lock(sentMutex);
                sent = std::move(frame);
                ++sentSeq;
            }
            if (transferMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(transferMs));
        }
};
