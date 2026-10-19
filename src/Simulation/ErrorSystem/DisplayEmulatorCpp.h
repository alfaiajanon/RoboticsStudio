#pragma once

#include "Emulator.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Utils/Log.h"
#include "Simulation/Controller/PinBus.h"
#include "Simulation/Controller/DeviceBus.h"
#include <vector>
#include <algorithm>

/*
 * TFT display with an ST7735-style controller (1.8" 160x128 modules), on the
 * board's SPI bus:
 *
 *   SPI            the real controller: bytes clocked in while CS is LOW are
 *                   commands while DC is LOW and parameters/pixels while it
 *                   is HIGH. Handled: SWRESET 0x01, SLPIN/SLPOUT 0x10/0x11,
 *                   INVOFF/INVON 0x20/0x21, DISPOFF/DISPON 0x28/0x29,
 *                   CASET 0x2A, RASET 0x2B, RAMWR 0x2C, MADCTL 0x36 (MX, MY,
 *                   MV, BGR), COLMOD 0x3A (16-bit RGB565 only). Others are
 *                   accepted and ignored. GRAM is the controller's native
 *                   128x160 portrait memory; the part's landscape screen shows
 *                   it rotated, as on a module mounted that way, so a sketch
 *                   with setRotation(1) draws upright. The panel is black while
 *                   asleep (as at power-on), switched off, held in reset (rst
 *                   LOW) or with the backlight pin LOW; a backlight pin driven
 *                   with PWM dims it; an undriven one counts as wired to VCC.
 *
 *   update()       Throttled at refresh_hz (physics thread): publishes the
 *                  panel into the display signal's ImageIOValue when it changed.
 *
 * SPI bytes arrive on the controller thread while the physics thread is
 * blocked (lockstep), so GRAM needs no lock.
 *
 * Parameters (component .rsdef -> emulator.parameters), read in init():
 *   refresh_hz   panel refresh rate, sim time   (default 30)
 */
class DisplayEmulatorCpp : public Emulator, public SPIDevice {
    Q_OBJECT

    private:
        QString signal;
        double refreshHz = 30.0;
        quint64 publishedSeq = 0;                 // physics thread only

        // ---- ST7735 controller
        int nativeCols = 128, nativeRows = 160;   // GRAM: the part's height x width
        std::vector<uint16_t> gram;
        bool sleeping = true, displayOn = false, inverted = false, resetHeld = false;
        uint8_t madctl = 0;
        int xs = 0, xe = 127, ys = 0, ye = 159;   // address window (logical)
        int px = 0, py = 0;                       // RAMWR position
        uint8_t command = 0;
        int paramIndex = 0;
        uint8_t pending = 0;                      // high byte of a pixel / parameter pair
        bool spiUsed = false, gramDirty = false;
        int csPin = -1, dcPin = -1, rstPin = -1, ledPin = -1;

        void resetController() {
            sleeping = true; displayOn = false; inverted = false; madctl = 0;
            xs = 0; xe = nativeCols - 1; ys = 0; ye = nativeRows - 1;
            px = py = 0; command = 0; paramIndex = 0;
            gramDirty = true;
        }

        void writePixel(uint16_t color) {
            int c = px, r = py;
            if (madctl & 0x20) std::swap(c, r);           // MV: row/column exchange
            if (madctl & 0x40) c = nativeCols - 1 - c;    // MX
            if (madctl & 0x80) r = nativeRows - 1 - r;    // MY
            if (c >= 0 && c < nativeCols && r >= 0 && r < nativeRows) {
                gram[size_t(r) * nativeCols + c] = color;
                gramDirty = true;
            }
            if (++px > xe) { px = xs; if (++py > ye) py = ys; }
        }

        void commandByte(uint8_t b) {
            command = b;
            paramIndex = 0;
            switch (b) {
                case 0x01: resetController(); break;               // SWRESET
                case 0x10: sleeping = true; gramDirty = true; break;
                case 0x11: sleeping = false; gramDirty = true; break;
                case 0x20: inverted = false; gramDirty = true; break;
                case 0x21: inverted = true; gramDirty = true; break;
                case 0x28: displayOn = false; gramDirty = true; break;
                case 0x29: displayOn = true; gramDirty = true; break;
                case 0x2C: px = xs; py = ys; break;                // RAMWR
                default: break;
            }
        }

        void dataByte(uint8_t b) {
            const int i = paramIndex++;
            switch (command) {
                case 0x2A:   // CASET: xs hi, xs lo, xe hi, xe lo
                case 0x2B: { // RASET
                    if (i % 2 == 0) { pending = b; break; }
                    const int v = (pending << 8) | b;
                    int& lo = command == 0x2A ? xs : ys;
                    int& hi = command == 0x2A ? xe : ye;
                    (i == 1 ? lo : hi) = v;
                    break;
                }
                case 0x2C:   // RAMWR: RGB565, high byte first
                    if (i % 2 == 0) pending = b;
                    else writePixel(uint16_t((pending << 8) | b));
                    break;
                case 0x36: madctl = b; gramDirty = true; break;
                default: break;   // COLMOD, frame rate, power, gamma...: accepted
            }
        }

        bool pinLow(int h) const {
            const PinState* s = pins ? pins->state(h) : nullptr;
            return s && s->drive == PinState::Drive::Level && s->level == 0;
        }

        // The panel as the viewer sees it (RGB888, the part's resolution).
        std::shared_ptr<QByteArray> renderGram() const {
            const int w = nativeRows, h = nativeCols;
            auto frame = std::make_shared<QByteArray>(w * h * 3, '\0');
            double brightness = 1.0;
            if (pins && pins->state(ledPin) && pins->state(ledPin)->drive != PinState::Drive::None) brightness = pins->dutyCycle(ledPin);
            if (sleeping || !displayOn || resetHeld || brightness <= 0.0) return frame;   // black
            uchar* out = reinterpret_cast<uchar*>(frame->data());
            for (int v = 0; v < h; ++v) {
                for (int u = 0; u < w; ++u) {
                    uint16_t c = gram[size_t(w - 1 - u) * nativeCols + v];   // out(u, v) = native(c = v, r = W-1-u)
                    if (inverted) c = uint16_t(~c);
                    int r8 = ((c >> 11) & 0x1f) * 255 / 31, g8 = ((c >> 5) & 0x3f) * 255 / 63, b8 = (c & 0x1f) * 255 / 31;
                    if (madctl & 0x08) std::swap(r8, b8);   // BGR panel order
                    uchar* p = out + (size_t(v) * w + u) * 3;
                    p[0] = uchar(r8 * brightness); p[1] = uchar(g8 * brightness); p[2] = uchar(b8 * brightness);
                }
            }
            return frame;
        }

    public:
        using Emulator::Emulator;

        void init() override {
            signal.clear();
            gram.clear();
            ComponentBlueprint* bp = component ? component->getBlueprint() : nullptr;
            if (!bp) return;

            const auto& params = bp->emulatorDef.parameters;
            refreshHz = params.value("refresh_hz", 30.0).toDouble();
            if (refreshHz <= 0.0) refreshHz = 30.0;

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
            nativeCols = img.height;
            nativeRows = img.width;
            gram.assign(size_t(nativeCols) * nativeRows, 0);
            spiUsed = false;
            resetHeld = false;
            resetController();
            if (pins) {
                csPin = pins->ensurePin(component->uid, "cs", "digital");
                dcPin = pins->ensurePin(component->uid, "dc", "digital");
                rstPin = pins->ensurePin(component->uid, "rst", "digital");
                ledPin = pins->ensurePin(component->uid, "led", "pwm");
                if (buses)
                    buses->addSPI(csPin, this, QString("ST7735 display (part %1)").arg(component->uid),
                                  pins->ensurePin(component->uid, "sck", "spi_sck"), pins->ensurePin(component->uid, "mosi", "spi_mosi"));
            }
            reset();
        }

        // ---- SPI (controller thread)
        void spiTransfer(const uint8_t* data, int len) override {
            if (gram.empty()) return;
            spiUsed = true;
            if (pinLow(rstPin)) { resetHeld = true; resetController(); return; }   // held in reset
            resetHeld = false;
            const bool isData = !pinLow(dcPin);
            for (int i = 0; i < len; ++i) {
                if (isData) dataByte(data[i]);
                else commandByte(data[i]);
            }
        }

        Schedule schedule() const override {
            return {Cadence::Throttled, 1.0 / refreshHz};
        }

        void update() override {
            // Nothing shows until the firmware first talks to the controller.
            if (signal.isEmpty() || !spiUsed) return;
            const bool held = pinLow(rstPin);
            if (held != resetHeld) { resetHeld = held; if (held) resetController(); gramDirty = true; }
            // Backlight changes show without new pixels.
            if (pins && pins->state(ledPin) && pins->state(ledPin)->drive != PinState::Drive::None) gramDirty = true;
            if (!gramDirty) return;
            gramDirty = false;
            ImageIOValue img = component->getImageValue(signal);
            img.format = ImageFormat::RGB888;
            img.time = simTime;
            img.seq = ++publishedSeq;
            img.pixels = renderGram();
            component->setImageValue(signal, img);
        }

        // Blank panel: no published frame.
        void reset() override {
            publishedSeq = 0;
            if (!signal.isEmpty()) {
                ImageIOValue img = component->getImageValue(signal);
                img.pixels.reset();
                img.seq = 0;
                img.time = 0.0;
                component->setImageValue(signal, img);
            }
        }
};
