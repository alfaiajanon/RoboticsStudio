/*
 * Stream.h: Arduino's Stream (reading with a timeout: readStringUntil,
 * parseInt, ...) and the board's serial ports, HardwareSerial.
 *
 * Serial is the board's USB port: what it prints appears in the Serial
 * Monitor and the Output panel, and what is typed into the monitor arrives
 * on it. On a real board (Real level) every byte takes 10 bits at the baud
 * rate on the wire, queued in the board's TX buffer (print() waits while it
 * is full), and bytes that come in wait in a receive buffer that overflows
 * if the sketch doesn't read it. ESP32 boards also have Serial1 and Serial2
 * on any pins: Serial1.begin(115200, SERIAL_8N1, rxPin, txPin).
 * Timeouts are simulated time (default 1000 ms).
 */
#ifndef STREAM_H
#define STREAM_H

#include "rs_abi.h"
#include "Print.h"

enum LookaheadMode { SKIP_ALL, SKIP_NONE, SKIP_WHITESPACE };
#define NO_IGNORE_CHAR '\x01'

class Stream : public Print {
    protected:
        unsigned long _timeout = 1000;
        int timedRead();
        int timedPeek();
        int peekNextDigit(LookaheadMode lookahead, bool detectDecimal);
        // Waits until a byte can be read or the time is up; false on timeout.
        virtual bool waitForByte(unsigned long timeoutMs);

    public:
        virtual int available() = 0;
        virtual int read() = 0;
        virtual int peek() = 0;

        void setTimeout(unsigned long timeout) { _timeout = timeout; }
        unsigned long getTimeout() const { return _timeout; }

        bool find(const char* target);
        bool find(const uint8_t* target) { return find(reinterpret_cast<const char*>(target)); }
        bool find(const char* target, size_t length);
        bool find(char target) { const char t[2] = {target, 0}; return find(t); }
        bool findUntil(const char* target, const char* terminator);

        long parseInt(LookaheadMode lookahead = SKIP_ALL, char ignore = NO_IGNORE_CHAR);
        float parseFloat(LookaheadMode lookahead = SKIP_ALL, char ignore = NO_IGNORE_CHAR);

        size_t readBytes(char* buffer, size_t length);
        size_t readBytes(uint8_t* buffer, size_t length) { return readBytes(reinterpret_cast<char*>(buffer), length); }
        size_t readBytesUntil(char terminator, char* buffer, size_t length);
        size_t readBytesUntil(char terminator, uint8_t* buffer, size_t length) {
            return readBytesUntil(terminator, reinterpret_cast<char*>(buffer), length);
        }
        String readString();
        String readStringUntil(char terminator);
};


// Frame formats for begin(). The simulator times every byte as 8N1 (10 bits).
#define SERIAL_5N1 0x00
#define SERIAL_6N1 0x02
#define SERIAL_7N1 0x04
#define SERIAL_8N1 0x06
#define SERIAL_5N2 0x08
#define SERIAL_6N2 0x0A
#define SERIAL_7N2 0x0C
#define SERIAL_8N2 0x0E
#define SERIAL_7E1 0x24
#define SERIAL_8E1 0x26
#define SERIAL_7O1 0x34
#define SERIAL_8O1 0x36

class HardwareSerial : public Stream {
    int port;

    protected:
        bool waitForByte(unsigned long timeoutMs) override;

    public:
        explicit HardwareSerial(int portNumber) : port(portNumber) {}

        // AVR: begin(baud) / begin(baud, config). ESP32 adds the pins (and
        // options the simulator ignores).
        void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rxPin = -1, int8_t txPin = -1,
                   bool invert = false, unsigned long timeoutMs = 20000UL, uint8_t rxfifoFullThreshold = 120);
        void end() { rs_uart_end(port); }
        int available() override { return rs_uart_available(port); }
        int peek() override { return rs_uart_peek(port); }
        int read() override { return rs_uart_read(port); }
        int availableForWrite() { return rs_uart_available_for_write(port); }
        void flush() { rs_uart_flush(port); }
        explicit operator bool() const { return true; }

        size_t write(uint8_t c) override;
        size_t write(const uint8_t* data, size_t size) override;
        using Print::write;
};

extern HardwareSerial Serial;
// ESP32 boards: two more UARTs on any pins (the GPIO matrix). An Uno has
// none, so a sketch that uses them doesn't compile for it, here or there.
#if defined(ESP32)
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;
#endif

#endif   // STREAM_H
