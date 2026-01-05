#pragma once


#include <QString>
#include <QMap>
#include "ComponentBlueprint.h"

class LibraryManager {
    private:
        LibraryManager(){}
        QMap<QString, ComponentBlueprint*> blueprints;
    
    public:
        static LibraryManager& getInstance();

        void load(const QString& catalogJsonPath);
        ComponentBlueprint* getBlueprint(const QString& model_id);
};