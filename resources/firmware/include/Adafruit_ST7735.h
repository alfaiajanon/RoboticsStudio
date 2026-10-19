/*
 * Adafruit_ST7735.h for RoboticsStudio: a compatible subset of Adafruit's
 * ST7735 TFT driver (our own implementation) for 1.8" 160x128 modules. It
 * sends the controller's real commands over the board's SPI bus, with CS and
 * DC driven by digitalWrite, so the same sketch drives a real module.
 *
 * Pins are int here (ideal-board numbers exceed 127); the real library takes
 * int8_t, which the same code fills fine on a real board.
 */
#ifndef ADAFRUIT_ST7735_H
#define ADAFRUIT_ST7735_H

#include <stdint.h>
#include "Adafruit_GFX.h"
#include "Adafruit_ST77xx.h"

#define INITR_GREENTAB    0x00
#define INITR_REDTAB      0x01
#define INITR_BLACKTAB    0x02
#define INITR_18GREENTAB  INITR_GREENTAB
#define INITR_18REDTAB    INITR_REDTAB
#define INITR_18BLACKTAB  INITR_BLACKTAB

#define ST7735_TFTWIDTH_128  128
#define ST7735_TFTHEIGHT_160 160

class Adafruit_ST7735 : public Adafruit_GFX {
    int csPin, dcPin, rstPin;
    bool inTransaction = false;

    void select();
    void deselect();
    void sendCommand(uint8_t command, const uint8_t* args = nullptr, uint8_t count = 0);
    void writeColor(uint16_t color, uint32_t count);

    public:
        Adafruit_ST7735(int cs, int dc, int rst);
        // Software-SPI form: the board's SPI pins are used either way.
        Adafruit_ST7735(int cs, int dc, int mosi, int sclk, int rst);

        void initR(uint8_t options = INITR_GREENTAB);
        void setRotation(uint8_t r) override;
        void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
        void invertDisplay(bool invert) override;
        void enableDisplay(bool enable);
        void enableSleep(bool enable);

        void drawPixel(int16_t x, int16_t y, uint16_t color) override;
        void startWrite() override;
        void endWrite() override;
        void writePixel(int16_t x, int16_t y, uint16_t color) override;
        void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
        void drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h) override;

        uint16_t color565(uint8_t r, uint8_t g, uint8_t b) const {
            return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
};

#endif   // ADAFRUIT_ST7735_H
