#pragma once
#include <QObject>



// Forward declaration to avoid circular dependency
class ComponentInstance; 



// Inheriting from QObject to allow subclasses to expose methods to JS
class BaseEmulator : public QObject {
    Q_OBJECT

    protected:
        ComponentInstance* component; 

    public:
        explicit BaseEmulator(ComponentInstance* comp, QObject* parent = nullptr) 
            : QObject(parent), component(comp) {}

        virtual ~BaseEmulator() = default;
        virtual void update() = 0; 
};