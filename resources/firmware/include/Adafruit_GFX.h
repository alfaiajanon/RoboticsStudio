/*
 * Adafruit_GFX.h for RoboticsStudio: a compatible subset of the Adafruit GFX
 * graphics API (our own implementation), so display sketches build both
 * here and with the real Adafruit libraries.
 *
 * Text uses a public-domain 5x7 font in a 6x8 cell, like the classic
 * Adafruit font, so layouts match. Characters outside ' '..'~' draw as '?'.
 * Colours are RGB565.
 */
#ifndef ADAFRUIT_GFX_H
#define ADAFRUIT_GFX_H

#include <stdint.h>
#include "Print.h"

class Adafruit_GFX : public Print {
    public:
        Adafruit_GFX(int16_t w, int16_t h);
        virtual ~Adafruit_GFX() = default;

        // The one primitive a display must provide.
        virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

        // Batched drawing (a display may speed these up).
        virtual void startWrite() {}
        virtual void writePixel(int16_t x, int16_t y, uint16_t color) { drawPixel(x, y, color); }
        virtual void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
        virtual void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { writeFillRect(x, y, 1, h, color); }
        virtual void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) { writeFillRect(x, y, w, 1, color); }
        virtual void writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
        virtual void endWrite() {}

        virtual void setRotation(uint8_t r);
        virtual void invertDisplay(bool invert) { (void)invert; }

        virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
        virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
        virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
        virtual void fillScreen(uint16_t color);
        virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
        virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

        void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
        void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
        void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
        virtual void drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h);
        void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h);

        void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size);
        void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
        void setTextColor(uint16_t c) { textcolor = textbgcolor = c; }   // transparent background
        void setTextColor(uint16_t c, uint16_t bg) { textcolor = c; textbgcolor = bg; }
        void setTextSize(uint8_t s) { textsize = s > 0 ? s : 1; }
        void setTextWrap(bool w) { wrap = w; }
        size_t write(uint8_t c) override;
        using Print::write;

        int16_t width() const { return _width; }
        int16_t height() const { return _height; }
        uint8_t getRotation() const { return rotation; }
        int16_t getCursorX() const { return cursor_x; }
        int16_t getCursorY() const { return cursor_y; }

    protected:
        int16_t WIDTH, HEIGHT;     // raw size, rotation 0
        int16_t _width, _height;   // size at the current rotation
        int16_t cursor_x = 0, cursor_y = 0;
        uint16_t textcolor = 0xFFFF, textbgcolor = 0xFFFF;
        uint8_t textsize = 1;
        uint8_t rotation = 0;
        bool wrap = true;
};

#endif   // ADAFRUIT_GFX_H
