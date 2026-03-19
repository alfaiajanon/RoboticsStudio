#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include "ComponentBlueprint.h"

struct CategoryDef {
    QString id;
    QString name;
    QStringList keys;
    QStringList modelIds;
};

class LibraryManager : public QObject {
    Q_OBJECT

    private:
        LibraryManager() {}

        QString path;
        QMap<QString, ComponentBlueprint*> blueprints;
        QList<CategoryDef> categories;
    
    public:
        static LibraryManager& getInstance();

        void load(const QString& catalogJsonPath);
        
        QString getCatalogPath() const { return path; }
        ComponentBlueprint* getBlueprint(const QString& model_id);
        const QList<CategoryDef>& getCategories() const { return categories; }

    signals:
        void catalogLoaded(const QString& loadedPath);
};