#include "Arduino.h"
#include "SPI.h"
#include "Adafruit_ST7735.h"

Adafruit_ST7735::Adafruit_ST7735(int cs, int dc, int rst)
    : Adafruit_GFX(ST7735_TFTWIDTH_128, ST7735_TFTHEIGHT_160), csPin(cs), dcPin(dc), rstPin(rst) {}

Adafruit_ST7735::Adafruit_ST7735(int cs, int dc, int mosi, int sclk, int rst)
    : Adafruit_ST7735(cs, dc, rst) { (void)mosi; (void)sclk; }

void Adafruit_ST7735::select() {
    if (!inTransaction) { SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0)); digitalWrite(csPin, LOW); }
}

void Adafruit_ST7735::deselect() {
    if (!inTransaction) { digitalWrite(csPin, HIGH); SPI.endTransaction(); }
}

void Adafruit_ST7735::sendCommand(uint8_t command, const uint8_t* args, uint8_t count) {
    select();
    digitalWrite(dcPin, LOW);
    SPI.transfer(command);
    digitalWrite(dcPin, HIGH);
    for (uint8_t i = 0; i < count; i++) SPI.transfer(args[i]);
    deselect();
}

// The controller's power-up sequence (the reset and wake-up delays are the
// datasheet's), 16-bit colour.
void Adafruit_ST7735::initR(uint8_t options) {
    (void)options;   // tab variants differ in offsets and colour order; one 160x128 panel here
    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);
    pinMode(dcPin, OUTPUT);
    digitalWrite(dcPin, HIGH);
    SPI.begin();
    if (rstPin >= 0) {
        pinMode(rstPin, OUTPUT);
        digitalWrite(rstPin, HIGH); delay(100);
        digitalWrite(rstPin, LOW);  delay(100);
        digitalWrite(rstPin, HIGH); delay(200);
    }
    sendCommand(ST77XX_SWRESET); delay(150);
    sendCommand(ST77XX_SLPOUT);  delay(255);
    const uint8_t colmod = 0x05;   // 16-bit RGB565
    sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
    setRotation(0);
    sendCommand(ST77XX_INVOFF);
    sendCommand(ST77XX_NORON); delay(10);
    sendCommand(ST77XX_DISPON); delay(100);
}

// MADCTL per rotation, as for Adafruit's black-tab modules.
void Adafruit_ST7735::setRotation(uint8_t r) {
    Adafruit_GFX::setRotation(r);
    uint8_t madctl = 0;
    switch (rotation) {
        case 0: madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_MY | ST77XX_MADCTL_RGB; break;
        case 1: madctl = ST77XX_MADCTL_MY | ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB; break;
        case 2: madctl = ST77XX_MADCTL_RGB; break;
        case 3: madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_MV | ST77XX_MADCTL_RGB; break;
    }
    sendCommand(ST77XX_MADCTL, &madctl, 1);
}

void Adafruit_ST7735::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    const uint16_t x1 = x + w - 1, y1 = y + h - 1;
    const uint8_t cols[4] = {uint8_t(x >> 8), uint8_t(x), uint8_t(x1 >> 8), uint8_t(x1)};
    const uint8_t rows[4] = {uint8_t(y >> 8), uint8_t(y), uint8_t(y1 >> 8), uint8_t(y1)};
    sendCommand(ST77XX_CASET, cols, 4);
    sendCommand(ST77XX_RASET, rows, 4);
    sendCommand(ST77XX_RAMWR);
}

void Adafruit_ST7735::invertDisplay(bool invert) { sendCommand(invert ? ST77XX_INVON : ST77XX_INVOFF); }
void Adafruit_ST7735::enableDisplay(bool enable) { sendCommand(enable ? ST77XX_DISPON : ST77XX_DISPOFF); }
void Adafruit_ST7735::enableSleep(bool enable) { sendCommand(enable ? ST77XX_SLPIN : ST77XX_SLPOUT); }

// Pixels after RAMWR, in chunks (one SPI transfer per chunk).
void Adafruit_ST7735::writeColor(uint16_t color, uint32_t count) {
    uint8_t chunk[64];
    for (int i = 0; i < 64; i += 2) { chunk[i] = uint8_t(color >> 8); chunk[i + 1] = uint8_t(color); }
    select();
    digitalWrite(dcPin, HIGH);
    while (count) {
        const uint32_t n = count > 32 ? 32 : count;
        SPI.transfer(chunk, n * 2);
        for (uint32_t i = 0; i < n * 2; i += 2) { chunk[i] = uint8_t(color >> 8); chunk[i + 1] = uint8_t(color); }   // transfer() overwrote it
        count -= n;
    }
    deselect();
}

void Adafruit_ST7735::startWrite() { select(); inTransaction = true; }
void Adafruit_ST7735::endWrite() { inTransaction = false; deselect(); }

void Adafruit_ST7735::writePixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    setAddrWindow(uint16_t(x), uint16_t(y), 1, 1);
    writeColor(color, 1);
}

void Adafruit_ST7735::drawPixel(int16_t x, int16_t y, uint16_t color) {
    startWrite(); writePixel(x, y, color); endWrite();
}

void Adafruit_ST7735::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;
    setAddrWindow(uint16_t(x), uint16_t(y), uint16_t(w), uint16_t(h));
    writeColor(color, uint32_t(w) * uint32_t(h));
}

void Adafruit_ST7735::drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h) {
    if (x < 0 || y < 0 || x + w > _width || y + h > _height) { Adafruit_GFX::drawRGBBitmap(x, y, bitmap, w, h); return; }
    startWrite();
    setAddrWindow(uint16_t(x), uint16_t(y), uint16_t(w), uint16_t(h));
    digitalWrite(dcPin, HIGH);
    uint8_t row[2 * 160];
    for (int16_t j = 0; j < h; j++) {
        int16_t i = 0;
        while (i < w) {
            const int16_t n = (w - i) > 160 ? 160 : (w - i);
            for (int16_t k = 0; k < n; k++) { row[2 * k] = uint8_t(bitmap[j * w + i + k] >> 8); row[2 * k + 1] = uint8_t(bitmap[j * w + i + k]); }
            SPI.transfer(row, size_t(n) * 2);
            i += n;
        }
    }
    endWrite();
}
