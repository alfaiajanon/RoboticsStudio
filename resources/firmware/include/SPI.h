/*
 * SPI.h: the Arduino SPI library, on the board's shared SPI bus. The bytes
 * go to every part whose chip-select pin is LOW (drive CS with
 * digitalWrite, as libraries such as Adafruit_ST7735 do).
 *
 * Nothing answers on MISO yet (no SPI sensor parts): transfer() returns 0.
 * At the Prototype level a transfer takes no simulated time; on a real board
 * it takes its bit time at the clock (beginTransaction()'s SPISettings, or
 * setClockDivider(); 4 MHz by default, as on an Uno), capped at the board's
 * maximum.
 */
#ifndef SPI_H
#define SPI_H

#include <stddef.h>
#include <stdint.h>
#include "rs_abi.h"

#define LSBFIRST 0
#define MSBFIRST 1
#define SPI_MODE0 0x00
#define SPI_MODE1 0x04
#define SPI_MODE2 0x08
#define SPI_MODE3 0x0C
#define SPI_CLOCK_DIV2 0x04
#define SPI_CLOCK_DIV4 0x00
#define SPI_CLOCK_DIV8 0x05
#define SPI_CLOCK_DIV16 0x01

class SPISettings {
    public:
        uint32_t clock = 4000000;
        SPISettings() {}
        SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) : clock(clock) { (void)bitOrder; (void)dataMode; }
};

class SPIClass {
    public:
        void begin() {}
        void end() {}
        void beginTransaction(SPISettings settings) { rs_spi_set_clock(settings.clock); }
        void endTransaction() {}
        uint8_t transfer(uint8_t data);
        uint16_t transfer16(uint16_t data);
        void transfer(void* buf, size_t count);   // sends buf, fills it with what came back (0s)
        void setBitOrder(uint8_t order) { (void)order; }
        void setDataMode(uint8_t mode) { (void)mode; }
        // The AVR divider codes, at a 16 MHz Uno clock.
        void setClockDivider(uint8_t div) {
            rs_spi_set_clock(div == SPI_CLOCK_DIV2 ? 8000000 : div == SPI_CLOCK_DIV8 ? 2000000 : div == SPI_CLOCK_DIV16 ? 1000000 : 4000000);
        }
};

extern SPIClass SPI;

#endif   // SPI_H
