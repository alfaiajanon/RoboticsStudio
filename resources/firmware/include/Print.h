/*
 * Print.h: Arduino's Print (print/println of text and numbers), then
 * Stream and the serial ports (Stream.h).
 */
#ifndef PRINT_H
#define PRINT_H

#include <stddef.h>
#include <stdint.h>
#include "WString.h"

class __FlashStringHelper;

class Print {
    size_t printNumber(unsigned long n, uint8_t base);
    size_t printFloat(double number, int digits);

    public:
        virtual ~Print() = default;
        virtual size_t write(uint8_t c) = 0;
        virtual size_t write(const uint8_t* data, size_t size);
        size_t write(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), strlenSafe(s)) : 0; }
        size_t write(const char* data, size_t size) { return write(reinterpret_cast<const uint8_t*>(data), size); }

        size_t print(const __FlashStringHelper* s) { return write(reinterpret_cast<const char*>(s)); }
        size_t print(const String& s) { return write(s.c_str(), s.length()); }
        size_t print(const char* s) { return write(s); }
        size_t print(char c) { return write(uint8_t(c)); }
        size_t print(unsigned char n, int base = 10) { return print(static_cast<unsigned long>(n), base); }
        size_t print(int n, int base = 10) { return print(static_cast<long>(n), base); }
        size_t print(unsigned int n, int base = 10) { return print(static_cast<unsigned long>(n), base); }
        size_t print(long n, int base = 10);
        size_t print(unsigned long n, int base = 10);
        size_t print(double n, int digits = 2) { return printFloat(n, digits); }

        size_t println() { return write("\r\n"); }
        template <class T>
        size_t println(const T& v) { size_t n = print(v); return n + println(); }
        template <class T>
        size_t println(const T& v, int format) { size_t n = print(v, format); return n + println(); }

        // Not on AVR (ESP32 and others have it): keep it out of code meant for an Uno.
        size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3)));

        static size_t strlenSafe(const char* s);
};


#include "Stream.h"
#endif   // PRINT_H
