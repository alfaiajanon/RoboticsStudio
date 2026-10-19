#include "Arduino.h"

#include <stdarg.h>
#include <stdio.h>

size_t Print::strlenSafe(const char* s) {
    return s ? strlen(s) : 0;
}

size_t Print::write(const uint8_t* data, size_t size) {
    size_t n = 0;
    while (size--) n += write(*data++);
    return n;
}

size_t Print::printNumber(unsigned long n, uint8_t base) {
    char buf[8 * sizeof(long) + 1];
    char* p = &buf[sizeof(buf) - 1];
    *p = '\0';
    if (base < 2) base = 10;
    do {
        const unsigned long digit = n % base;
        n /= base;
        *--p = char(digit < 10 ? '0' + digit : 'A' + digit - 10);
    } while (n);
    return write(p);
}

size_t Print::print(long n, int base) {
    if (base == 0) return write(uint8_t(n));
    if (base == 10 && n < 0) {
        size_t t = print('-');
        return t + printNumber(0UL - static_cast<unsigned long>(n), 10);
    }
    return printNumber(static_cast<unsigned long>(n), uint8_t(base));
}

size_t Print::print(unsigned long n, int base) {
    if (base == 0) return write(uint8_t(n));
    return printNumber(n, uint8_t(base));
}

size_t Print::printFloat(double number, int digits) {
    if (isnan(number)) return write("nan");
    if (isinf(number)) return write("inf");
    char buf[48];
    snprintf(buf, sizeof buf, "%.*f", digits < 0 ? 0 : digits, number);
    return write(buf);
}

size_t Print::printf(const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof buf, format, args);
    va_end(args);
    if (n < 0) return 0;
    return write(buf, size_t(n) < sizeof buf ? size_t(n) : sizeof buf - 1);
}


size_t HardwareSerial::write(uint8_t c) {
    rs_serial_write(reinterpret_cast<const char*>(&c), 1);
    return 1;
}

size_t HardwareSerial::write(const uint8_t* data, size_t size) {
    if (size) rs_serial_write(reinterpret_cast<const char*>(data), uint32_t(size));
    return size;
}
