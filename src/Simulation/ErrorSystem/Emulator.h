#pragma once

#include <QObject>


class ComponentInstance; 



// Inheriting from QObject to allow subclasses to expose methods to JS
class Emulator : public QObject {
    Q_OBJECT

    protected:
        ComponentInstance* component; 

    public:
        explicit Emulator(ComponentInstance* comp, QObject* parent = nullptr) 
            : QObject(parent), component(comp) {}

        virtual ~Emulator() = default;
        virtual void update() = 0; 
        virtual void reset() = 0;
};