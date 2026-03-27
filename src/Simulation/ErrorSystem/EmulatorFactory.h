#pragma once

#include <QString>

class ComponentInstance;
class Emulator; 


class EmulatorFactory {
public:
    static Emulator* create(const QString& type, ComponentInstance* comp);
};