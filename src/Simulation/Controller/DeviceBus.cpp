#include "DeviceBus.h"

#include "Simulation/Controller/PinBus.h"
#include "Utils/Log.h"



void DeviceBus::reset(PinBus* pinBus) {
    i2c.clear();
    spi.clear();
    cameras.clear();
    unpowered.clear();
    pins = pinBus;
    restricted = false;
    i2cSda = i2cScl = spiSck = spiMosi = -1;
}



void DeviceBus::restrictTo(int sdaHandle, int sclHandle, int sckHandle, int mosiHandle) {
    restricted = true;
    i2cSda = sdaHandle;
    i2cScl = sclHandle;
    spiSck = sckHandle;
    spiMosi = mosiHandle;
}



void DeviceBus::addI2C(int address, I2CDevice* device, const QString& who, int sdaHandle, int sclHandle) {
    if (restricted && (i2cSda < 0 || sdaHandle != i2cSda || sclHandle != i2cScl)) {
        Log::warning(QString("%1: SDA/SCL are not wired to the board's I2C pins; it won't answer").arg(who));
        return;
    }
    if (i2c.contains(address)) {
        Log::warning(QString("%1: I2C address 0x%2 is already taken on the board's bus; this part won't answer")
                         .arg(who).arg(address, 2, 16, QChar('0')));
        return;
    }
    i2c.insert(address, device);
}



void DeviceBus::addSPI(int csHandle, SPIDevice* device, const QString& who, int sckHandle, int mosiHandle) {
    if (restricted && (spiSck < 0 || sckHandle != spiSck || mosiHandle != spiMosi)) {
        Log::warning(QString("%1: SCK/MOSI are not wired to the board's SPI pins; it won't receive anything").arg(who));
        return;
    }
    spi.append({csHandle, device});
}



int DeviceBus::i2cWrite(int address, const uint8_t* data, int len, bool stop) {
    I2CDevice* d = i2c.value(address, nullptr);
    if (!d || unpowered.contains(d)) return 2;   // address NACK
    return d->i2cWrite(data, len, stop) ? 0 : 3;   // 3 = data NACK
}



int DeviceBus::i2cRead(int address, uint8_t* data, int len) {
    I2CDevice* d = i2c.value(address, nullptr);
    return d && !unpowered.contains(d) ? d->i2cRead(data, len) : 0;
}



// A device listens while its CS pin is driven LOW (an undriven CS floats:
// nobody is selected).
void DeviceBus::spiTransfer(const uint8_t* data, int len) {
    if (!pins) return;
    for (const auto& [cs, device] : spi) {
        const PinState* s = pins->state(cs);
        if (s && s->drive == PinState::Drive::Level && s->level == 0 && !unpowered.contains(device)) device->spiTransfer(data, len);
    }
}
