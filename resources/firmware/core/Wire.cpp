#include "Arduino.h"
#include "Wire.h"

TwoWire Wire;

void TwoWire::beginTransmission(uint8_t address) {
    txAddress = address;
    txLength = 0;
    txOverflow = false;
    transmitting = true;
}

size_t TwoWire::write(uint8_t data) {
    if (!transmitting) return 0;
    if (txLength >= BUFFER_LENGTH) { txOverflow = true; return 0; }
    txBuffer[txLength++] = data;
    return 1;
}

size_t TwoWire::write(const uint8_t* data, size_t quantity) {
    size_t n = 0;
    while (n < quantity && write(data[n])) ++n;
    return n;
}

uint8_t TwoWire::endTransmission(bool sendStop) {
    transmitting = false;
    if (txOverflow) return 1;
    return uint8_t(rs_i2c_write(txAddress, txBuffer, uint32_t(txLength), sendStop ? 1 : 0));
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, bool sendStop) {
    (void)sendStop;
    if (quantity > BUFFER_LENGTH) quantity = BUFFER_LENGTH;
    rxLength = size_t(rs_i2c_read(address, rxBuffer, quantity));
    rxIndex = 0;
    return uint8_t(rxLength);
}
