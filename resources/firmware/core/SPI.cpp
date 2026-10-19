#include "Arduino.h"
#include "SPI.h"

SPIClass SPI;

uint8_t SPIClass::transfer(uint8_t data) {
    rs_spi_transfer(&data, 1);
    return 0;
}

uint16_t SPIClass::transfer16(uint16_t data) {
    const uint8_t bytes[2] = {uint8_t(data >> 8), uint8_t(data & 0xff)};   // MSB first
    rs_spi_transfer(bytes, 2);
    return 0;
}

void SPIClass::transfer(void* buf, size_t count) {
    if (!count) return;
    rs_spi_transfer(static_cast<const uint8_t*>(buf), uint32_t(count));
    memset(buf, 0, count);
}
