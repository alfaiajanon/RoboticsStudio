#pragma once

#include <QList>
#include <QMap>
#include <QSet>
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
 * The boards' buses for one run. Each board is a master with its own I2C bus
 * (devices by address) and SPI bus (devices by chip-select pin); the ideal
 * board is one master that reaches every part. Transaction level: bytes go to
 * the device that a real bus would deliver them to, and at the Prototype
 * level a transfer takes no time (bus timing comes with the Real level).
 *
 * Built at Play like PinBus: masters first (reset() + addMaster()), then the
 * emulators register in init(). Same threading as PinBus: one controller
 * thread at a time calls in while the physics thread is blocked in
 * LockstepGate::runStep, and devices read what they received in update().
 */
class DeviceBus {
    struct Master {
        int boardUid = -1;
        // Real level: the board's bus pins (PinBus handles); a device whose
        // bus pins aren't on them is not on its bus. Unrestricted on the
        // ideal board.
        bool restricted = false;
        int i2cSda = -1, i2cScl = -1, spiSck = -1, spiMosi = -1;
        QMap<int, I2CDevice*> i2c;                // address -> device
        QList<QPair<int, SPIDevice*>> spi;        // CS pin handle (PinBus) -> device
    };
    QList<Master> masters;
    QMap<int, CameraDevice*> cameras;         // SCCB data pin handle -> camera
    PinBus* pins = nullptr;
    // Devices whose part has no supply right now (Real level): they don't answer.
    QSet<const void*> unpowered;

    bool valid(int m) const { return m >= 0 && m < masters.size(); }

    public:
        void reset(PinBus* pinBus);
        // A board's buses: its hardware bus pins (PinBus handles, -1 = it has
        // none), or unrestricted (the ideal board). Returns the master index.
        int addMaster(int boardUid, bool restricted, int sdaHandle = -1, int sclHandle = -1,
                      int sckHandle = -1, int mosiHandle = -1);
        int masterOf(int boardUid) const;   // -1 if the board has no master

        // Part side, in Emulator::init(), with the handles of the part's own
        // bus pins. `who` names the part in warnings.
        void addI2C(int address, I2CDevice* device, const QString& who, int sdaHandle, int sclHandle);
        void addSPI(int csHandle, SPIDevice* device, const QString& who, int sckHandle, int mosiHandle);
        void addCamera(int sdaHandle, CameraDevice* camera) { cameras.insert(sdaHandle, camera); }
        CameraDevice* camera(int sdaHandle) const {
            CameraDevice* c = cameras.value(sdaHandle, nullptr);
            return unpowered.contains(c) ? nullptr : c;
        }
        // Physics thread, between steps: a device's part lost (or regained)
        // its supply. Pass the device as the bus interface it registered with.
        void setPowered(const void* device, bool powered) {
            if (powered) unpowered.remove(device); else unpowered.insert(device);
        }

        // Firmware side (Wire / SPI), on the board's master. i2cWrite returns
        // Wire's endTransmission() status: 0 = ok, 2 = no device at the address.
        int i2cWrite(int master, int address, const uint8_t* data, int len, bool stop);
        int i2cRead(int master, int address, uint8_t* data, int len);   // bytes read; 0 = no device
        void spiTransfer(int master, const uint8_t* data, int len);     // to every device whose CS is LOW
};
