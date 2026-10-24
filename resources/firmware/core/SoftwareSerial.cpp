#include "SoftwareSerial.h"

// The SoftwareSerial that receives (the AVR library keeps the same pointer).
static SoftwareSerial* activeObject = nullptr;


SoftwareSerial::SoftwareSerial(int receivePin, int transmitPin, bool) : rxPin(receivePin), txPin(transmitPin) {}

void SoftwareSerial::begin(long speed) {
    if (port < 0) port = rs_soft_serial_open(rxPin, txPin);
    rs_uart_begin(port, uint32_t(speed), -1, -1);   // listens, as the library's begin() does
    activeObject = this;
}

bool SoftwareSerial::listen() {
    if (port < 0) return false;
    activeObject = this;
    return rs_soft_serial_listen(port) != 0;
}

void SoftwareSerial::end() {
    stopListening();
    if (port >= 0) rs_uart_end(port);
}

bool SoftwareSerial::isListening() {
    return activeObject == this;
}

bool SoftwareSerial::stopListening() {
    if (activeObject != this) return false;
    activeObject = nullptr;
    rs_soft_serial_listen(-1);
    return true;
}

size_t SoftwareSerial::write(uint8_t byte) {
    return port >= 0 ? size_t(rs_uart_write(port, &byte, 1)) : 0;
}

bool SoftwareSerial::waitForByte(unsigned long timeoutMs) {
    if (port < 0) return false;
    return rs_uart_wait(port, uint32_t(timeoutMs > 4000000UL ? 4000000000UL : timeoutMs * 1000UL)) != 0;
}
