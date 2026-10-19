/*
 * Wire.h: the Arduino I2C library, on the board's shared I2C bus. Parts
 * answer at their I2C address (an MPU6050 at 0x68).
 *
 * As on an Uno, the buffers hold 32 bytes: a longer write fails
 * (endTransmission() returns 1) and requestFrom() gives at most 32.
 * At the Prototype level a transaction takes no simulated time; on a real
 * board it takes its bit time at the bus clock (100 kHz unless setClock()).
 * begin(sda, scl) (ESP32) is accepted: which pins the bus uses comes from
 * the board and the wiring.
 */
#ifndef WIRE_H
#define WIRE_H

#include <stddef.h>
#include <stdint.h>
#include "Print.h"
#include "rs_abi.h"

#define BUFFER_LENGTH 32
#define WIRE_HAS_END 1

class TwoWire : public Print {
    uint8_t txAddress = 0;
    uint8_t txBuffer[BUFFER_LENGTH];
    size_t txLength = 0;
    bool txOverflow = false;
    bool transmitting = false;
    uint8_t rxBuffer[BUFFER_LENGTH];
    size_t rxLength = 0;
    size_t rxIndex = 0;

    public:
        void begin() {}
        void begin(uint8_t address) { (void)address; }   // target (slave) mode is not simulated
        bool begin(int sda, int scl, uint32_t frequency = 0) { (void)sda; (void)scl; if (frequency) setClock(frequency); return true; }
        void end() {}
        void setClock(uint32_t hz) { rs_i2c_set_clock(hz); }
        void setWireTimeout(uint32_t timeout = 25000, bool reset = false) { (void)timeout; (void)reset; }

        void beginTransmission(uint8_t address);
        void beginTransmission(int address) { beginTransmission(uint8_t(address)); }
        // 0 ok, 1 data too long, 2 NACK on the address (no such device), 3 NACK on data.
        uint8_t endTransmission(bool sendStop = true);
        uint8_t requestFrom(uint8_t address, uint8_t quantity, bool sendStop = true);
        uint8_t requestFrom(int address, int quantity, int sendStop = 1) { return requestFrom(uint8_t(address), uint8_t(quantity), sendStop != 0); }

        size_t write(uint8_t data) override;
        size_t write(const uint8_t* data, size_t quantity) override;
        using Print::write;
        int available() { return int(rxLength - rxIndex); }
        int read() { return rxIndex < rxLength ? rxBuffer[rxIndex++] : -1; }
        int peek() { return rxIndex < rxLength ? rxBuffer[rxIndex] : -1; }
        void flush() {}
};

extern TwoWire Wire;

#endif   // WIRE_H
