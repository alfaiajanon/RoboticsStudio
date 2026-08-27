#pragma once

#include <QString>
#include "Emulator.h"

class ComponentInstance;


class EmulatorFactory {
public:
    static Emulator* create(const QString& type, ComponentInstance* comp);
};