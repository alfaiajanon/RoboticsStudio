/*
 * SoftwareSerial.h: the AVR SoftwareSerial library's API. A serial port on
 * any two pins, timed by the CPU instead of a UART: it sends each byte bit
 * by bit before write() returns (no TX buffer), and only one SoftwareSerial
 * receives at a time, the one that listen()ed last (begin() listens). Its
 * receive buffer holds 64 bytes; overflow() says if bytes were lost.
 * On a real Uno reception is unreliable above about 57600 baud; the
 * simulator warns there but receives anyway.
 *
 * ESP32 cores have no SoftwareSerial: use Serial1/Serial2 on any pins.
 */
#ifndef SOFTWARE_SERIAL_H
#define SOFTWARE_SERIAL_H

#if defined(ESP32)
#error "SoftwareSerial is an AVR library: the ESP32 core has none. Use Serial1 or Serial2 on any pins: Serial1.begin(baud, SERIAL_8N1, rxPin, txPin)"
#endif

#include "Arduino.h"

class SoftwareSerial : public Stream {
    int rxPin, txPin;
    int port = -1;   // the simulator's port, from begin()

    protected:
        bool waitForByte(unsigned long timeoutMs) override;

    public:
        // Pins are int here (the ideal board's numbers go above 255); an AVR
        // sketch's uint8_t pins convert.
        SoftwareSerial(int receivePin, int transmitPin, bool inverseLogic = false);
        ~SoftwareSerial() override { end(); }

        void begin(long speed);
        bool listen();
        void end();
        bool isListening();
        bool stopListening();
        bool overflow() { return port >= 0 && rs_soft_serial_overflow(port) != 0; }
        int peek() override { return port >= 0 ? rs_uart_peek(port) : -1; }

        size_t write(uint8_t byte) override;
        using Print::write;
        int read() override { return port >= 0 ? rs_uart_read(port) : -1; }
        int available() override { return port >= 0 ? rs_uart_available(port) : 0; }
        void flush() {}
        explicit operator bool() { return true; }
};

#endif   // SOFTWARE_SERIAL_H
