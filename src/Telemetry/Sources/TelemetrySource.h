#pragma once

#include <QString>
#include <any>
#include <qobject.h>
#include "mujoco/mujoco.h"





enum class DataType { SCALAR, VECTOR, IMAGE };
enum class SourceKind { INPUT, OUTPUT, MCU, BODY_MOTION, CUSTOM };




/*
 * Producer-side abstraction for telemetry data.
 * A TelemetrySource owns (or feeds) a registered channel and knows how to
 * extract its data from the live MuJoCo state. The physics thread calls
 * capture() after stepping; the UI only ever sees the channel.
 * Subclass this to add new telemetry kinds (body motion, forces, energy, ...).
 */

class TelemetrySource {
    QString unique_key;
    DataType dataType;
    std::atomic<int> refCount{0};

    public:
        QString name;
        QString description;

        virtual ~TelemetrySource() = default;
        void setKey(QString key)            { unique_key=key; }
        void setDataType(DataType type)     { dataType=type; }
        QString getKey()                    { return unique_key; }
        DataType getDataType()              { return dataType; }

        void subscribe()                    { refCount.fetch_add(1); }
        void unsubscribe()                  { refCount.fetch_sub(1); }
        bool isActive() const               { return refCount.load() > 0; }

        virtual void capture(double time) = 0;
        virtual std::any snapshotAll() = 0;
        // virtual void* snapshotFrom(double time)=0;
};
