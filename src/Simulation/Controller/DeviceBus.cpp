#include "DeviceBus.h"

#include "Simulation/Controller/PinBus.h"
#include "Utils/Log.h"



void DeviceBus::reset(PinBus* pinBus) {
    masters.clear();
    cameras.clear();
    unpowered.clear();
    pins = pinBus;
}



int DeviceBus::addMaster(int boardUid, bool restricted, int sdaHandle, int sclHandle, int sckHandle, int mosiHandle) {
    Master m;
    m.boardUid = boardUid;
    m.restricted = restricted;
    m.i2cSda = sdaHandle;
    m.i2cScl = sclHandle;
    m.spiSck = sckHandle;
    m.spiMosi = mosiHandle;
    masters.append(m);
    return int(masters.size()) - 1;
}



int DeviceBus::masterOf(int boardUid) const {
    for (int i = 0; i < masters.size(); ++i) if (masters[i].boardUid == boardUid) return i;
    return -1;
}



// On every board whose I2C pins the part's SDA/SCL share (usually one).
void DeviceBus::addI2C(int address, I2CDevice* device, const QString& who, int sdaHandle, int sclHandle) {
    bool onBus = false;
    for (Master& m : masters) {
        if (m.restricted && (m.i2cSda < 0 || sdaHandle != m.i2cSda || sclHandle != m.i2cScl)) continue;
        onBus = true;
        if (m.i2c.contains(address)) {
            Log::warning(QString("%1: I2C address 0x%2 is already taken on the board's bus; this part won't answer")
                             .arg(who).arg(address, 2, 16, QChar('0')));
            continue;
        }
        m.i2c.insert(address, device);
    }
    if (!onBus && !masters.isEmpty())
        Log::warning(QString("%1: SDA/SCL are not wired to a board's I2C pins; it won't answer").arg(who));
}



void DeviceBus::addSPI(int csHandle, SPIDevice* device, const QString& who, int sckHandle, int mosiHandle) {
    bool onBus = false;
    for (Master& m : masters) {
        if (m.restricted && (m.spiSck < 0 || sckHandle != m.spiSck || mosiHandle != m.spiMosi)) continue;
        onBus = true;
        m.spi.append({csHandle, device});
    }
    if (!onBus && !masters.isEmpty())
        Log::warning(QString("%1: SCK/MOSI are not wired to a board's SPI pins; it won't receive anything").arg(who));
}



int DeviceBus::i2cWrite(int master, int address, const uint8_t* data, int len, bool stop) {
    I2CDevice* d = valid(master) ? masters[master].i2c.value(address, nullptr) : nullptr;
    if (!d || unpowered.contains(d)) return 2;   // address NACK
    return d->i2cWrite(data, len, stop) ? 0 : 3;   // 3 = data NACK
}



int DeviceBus::i2cRead(int master, int address, uint8_t* data, int len) {
    I2CDevice* d = valid(master) ? masters[master].i2c.value(address, nullptr) : nullptr;
    return d && !unpowered.contains(d) ? d->i2cRead(data, len) : 0;
}



// A device listens while its CS pin is driven LOW (an undriven CS floats:
// nobody is selected).
void DeviceBus::spiTransfer(int master, const uint8_t* data, int len) {
    if (!pins || !valid(master)) return;
    for (const auto& [cs, device] : masters[master].spi) {
        const PinState* s = pins->state(cs);
        if (s && s->drive == PinState::Drive::Level && s->level == 0 && !unpowered.contains(device)) device->spiTransfer(data, len);
    }
}
