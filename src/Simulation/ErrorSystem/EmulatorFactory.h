#pragma once

#include <QString>
#include <QStringList>
#include "Emulator.h"

class ComponentInstance;


class EmulatorFactory {
public:
    static Emulator* create(const QString& type, ComponentInstance* comp);
    // Every type create() accepts, for editors. Keep in sync with create().
    static QStringList types();
};