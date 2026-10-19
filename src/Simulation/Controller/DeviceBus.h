#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>
#include <memory>
#include <QByteArray>

class PinBus;


// A part that answers on the board's I2C bus at its address.
class I2CDevice {
    public:
        virtual ~I2CDevice() = default;
        // A write transaction (register pointer, then data); `stop` false =
        // a repeated start follows. Returns false to NACK.
        virtual bool i2cWrite(const uint8_t* data, int len, bool stop) = 0;
        // A read transaction: fill `data`; returns the bytes sent.
        virtual int i2cRead(uint8_t* data, int len) = 0;
};


// A part on the board's SPI bus: it takes the bytes clocked out while its
// chip-select pin is LOW.
class SPIDevice {
    public:
        virtual ~SPIDevice() = default;
        virtual void spiTransfer(const uint8_t* data, int len) = 0;
};


// The newest frame a camera has delivered (shared, immutable pixels).
struct CameraFrame {
    std::shared_ptr<const QByteArray> pixels;   // RGB888, or float32 meters for depth; null = none yet
    int width = 0, height = 0;
    bool depth = false;
    quint64 seq = 0;
    double time = 0.0;   // capture time, sim seconds
};

// A camera module, reached through its SCCB (I2C) data pin: the camera
// driver's config names that pin.
class CameraDevice {
    public:
        virtual ~CameraDevice() = default;
        virtual int frameWidth() const = 0;
        virtual int frameHeight() const = 0;
        virtual bool isDepth() const = 0;
        virtual CameraFrame latestFrame() const = 0;
};


/*
 * The ideal board's shared buses for one run: one I2C bus (devices by
 * address) and one SPI bus (devices by chip-select pin). Transaction level:
 * bytes go to the device that a real bus would deliver them to, and at the
 * Prototype level a transfer takes no time (bus timing comes with the Real
 * level).
 *
 * Built at Play like PinBus: emulators register in init(). Same threading as
 * PinBus: the controller thread calls in while the physics thread is blocked
 * in LockstepGate::runStep, and devices read what they received in update().
 */
class DeviceBus {
    QMap<int, I2CDevice*> i2c;                // address -> device
    QList<QPair<int, SPIDevice*>> spi;        // CS pin handle (PinBus) -> device
    QMap<int, CameraDevice*> cameras;         // SCCB data pin handle -> camera
    PinBus* pins = nullptr;
    // Real level: the board's bus pins (PinBus handles); a device whose bus
    // pins aren't on them is not on the bus. Unrestricted on the ideal board.
    bool restricted = false;
    int i2cSda = -1, i2cScl = -1, spiSck = -1, spiMosi = -1;

    public:
        void reset(PinBus* pinBus);
        // Real level, after reset(): the board's hardware bus pins (-1 = it has none).
        void restrictTo(int sdaHandle, int sclHandle, int sckHandle, int mosiHandle);

        // Part side, in Emulator::init(), with the handles of the part's own
        // bus pins. `who` names the part in warnings.
        void addI2C(int address, I2CDevice* device, const QString& who, int sdaHandle, int sclHandle);
        void addSPI(int csHandle, SPIDevice* device, const QString& who, int sckHandle, int mosiHandle);
        void addCamera(int sdaHandle, CameraDevice* camera) { cameras.insert(sdaHandle, camera); }
        CameraDevice* camera(int sdaHandle) const { return cameras.value(sdaHandle, nullptr); }

        // Firmware side (Wire / SPI). i2cWrite returns Wire's
        // endTransmission() status: 0 = ok, 2 = no device at the address.
        int i2cWrite(int address, const uint8_t* data, int len, bool stop);
        int i2cRead(int address, uint8_t* data, int len);   // bytes read; 0 = no device
        void spiTransfer(const uint8_t* data, int len);     // to every device whose CS is LOW
};
