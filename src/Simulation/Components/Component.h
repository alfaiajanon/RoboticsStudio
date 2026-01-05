#pragma once

#include <bits/stdc++.h>
#include <QString>
#include <QList>    
#include "ComponentBlueprint.h"
#include "Simulation/Controllers/SimController.h"

using namespace std;



class ComponentInstance{
    public:
        int uid;
        int parentUid;

        QString name;
        QString type;
        QString model;

        QString parentConnector;
        QString selfConnector;

        ComponentBlueprint* blueprint;
        SimController* controller;

        QList<ComponentInstance*> children;

        ComponentInstance() : uid(-1), parentUid(-1) {}
};





// class Component {
    
//     protected:
//         int id;
//         QString name;
//         QString version;
//         QString author;
//         QString iconPath;
//         QString mujocoModelPath;

//     public:
//         Component(){}   
    
//         int attachToPin(int pinNumber, Component* component) {
            
//         }

//         virtual int apply();


//         // XML Generation
//         QString getBodyXML();
//         QString getAssetXML();
//         QString getConstraintXML();
// };