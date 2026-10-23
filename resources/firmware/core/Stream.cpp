// Stream's timed reads, after the Arduino core's Stream.cpp; the waiting is
// simulated time (rs_uart_wait on a serial port).
#include "Arduino.h"


bool Stream::waitForByte(unsigned long timeoutMs) {
    const unsigned long start = millis();
    while (available() <= 0) {
        if (millis() - start >= timeoutMs) return false;
        delayMicroseconds(50);
    }
    return true;
}

int Stream::timedRead() {
    int c = read();
    if (c >= 0) return c;
    return waitForByte(_timeout) ? read() : -1;
}

int Stream::timedPeek() {
    int c = peek();
    if (c >= 0) return c;
    return waitForByte(_timeout) ? peek() : -1;
}

// The next char that can start a number, skipping per `lookahead`; -1 on timeout.
int Stream::peekNextDigit(LookaheadMode lookahead, bool detectDecimal) {
    for (;;) {
        const int c = timedPeek();
        if (c < 0 || c == '-' || (c >= '0' && c <= '9') || (detectDecimal && c == '.')) return c;
        switch (lookahead) {
            case SKIP_NONE: return -1;
            case SKIP_WHITESPACE:
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return -1;
                break;
            case SKIP_ALL: break;
        }
        read();
    }
}

bool Stream::find(const char* target) {
    return findUntil(target, nullptr);
}

bool Stream::find(const char* target, size_t length) {
    size_t matched = 0;
    if (length == 0) return true;
    for (;;) {
        const int c = timedRead();
        if (c < 0) return false;
        if (c == target[matched]) {
            if (++matched == length) return true;
        } else {
            matched = c == target[0] ? 1 : 0;
        }
    }
}

bool Stream::findUntil(const char* target, const char* terminator) {
    const size_t tlen = target ? strlen(target) : 0;
    const size_t elen = terminator ? strlen(terminator) : 0;
    if (tlen == 0) return true;
    size_t matched = 0, ended = 0;
    for (;;) {
        const int c = timedRead();
        if (c < 0) return false;
        if (c == target[matched]) {
            if (++matched == tlen) return true;
        } else {
            matched = c == target[0] ? 1 : 0;
        }
        if (elen) {
            if (c == terminator[ended]) {
                if (++ended == elen) return false;
            } else {
                ended = c == terminator[0] ? 1 : 0;
            }
        }
    }
}

long Stream::parseInt(LookaheadMode lookahead, char ignore) {
    bool negative = false;
    long value = 0;
    int c = peekNextDigit(lookahead, false);
    if (c < 0) return 0;
    do {
        if (c == ignore) {}
        else if (c == '-') negative = true;
        else if (c >= '0' && c <= '9') value = value * 10 + c - '0';
        read();
        c = timedPeek();
    } while ((c >= '0' && c <= '9') || c == ignore);
    return negative ? -value : value;
}

float Stream::parseFloat(LookaheadMode lookahead, char ignore) {
    bool negative = false, fraction = false;
    long value = 0;
    float scale = 1.0f;
    int c = peekNextDigit(lookahead, true);
    if (c < 0) return 0.0f;
    do {
        if (c == ignore) {}
        else if (c == '-') negative = true;
        else if (c == '.') fraction = true;
        else if (c >= '0' && c <= '9') {
            value = value * 10 + c - '0';
            if (fraction) scale *= 0.1f;
        }
        read();
        c = timedPeek();
    } while ((c >= '0' && c <= '9') || (c == '.' && !fraction) || c == ignore);
    const float v = fraction ? value * scale : float(value);
    return negative ? -v : v;
}

size_t Stream::readBytes(char* buffer, size_t length) {
    size_t n = 0;
    while (n < length) {
        const int c = timedRead();
        if (c < 0) break;
        buffer[n++] = char(c);
    }
    return n;
}

size_t Stream::readBytesUntil(char terminator, char* buffer, size_t length) {
    size_t n = 0;
    while (n < length) {
        const int c = timedRead();
        if (c < 0 || c == terminator) break;
        buffer[n++] = char(c);
    }
    return n;
}

String Stream::readString() {
    String s;
    for (int c = timedRead(); c >= 0; c = timedRead()) s += char(c);
    return s;
}

String Stream::readStringUntil(char terminator) {
    String s;
    for (int c = timedRead(); c >= 0 && c != terminator; c = timedRead()) s += char(c);
    return s;
}



void HardwareSerial::begin(unsigned long baud, uint32_t, int8_t rxPin, int8_t txPin, bool, unsigned long, uint8_t) {
    rs_uart_begin(port, uint32_t(baud), rxPin, txPin);
}

bool HardwareSerial::waitForByte(unsigned long timeoutMs) {
    return rs_uart_wait(port, uint32_t(timeoutMs > 4000000UL ? 4000000000UL : timeoutMs * 1000UL)) != 0;
}

size_t HardwareSerial::write(uint8_t c) {
    return size_t(rs_uart_write(port, &c, 1));
}

size_t HardwareSerial::write(const uint8_t* data, size_t size) {
    return size ? size_t(rs_uart_write(port, data, uint32_t(size))) : 0;
}
