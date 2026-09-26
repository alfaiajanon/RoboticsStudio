#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <qobject.h>
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

        QString dir;
        QMap<QString, ComponentBlueprint*> blueprints;
        QList<CategoryDef> categories;

        QString copyResourceFile(const QString& srcAbsPath, const QString& destDir, const QString& subfolder);


    public:
        static LibraryManager& getInstance();

        bool load(const QString& models_dir="");
        bool loadLocalComponents(QString project_dir);
        bool saveLocalComponent(ComponentData draft);
        bool deleteLocalComponent(QString modelId);

        QString getModelsDir();
        bool hasBlueprint(const QString& model_id);
        ComponentBlueprint* getBlueprint(const QString& model_id);
        const QList<CategoryDef>& getCategories() const { return categories; }

    signals:
        void catalogLoaded();
        void componentSaved(const QString& modelId);
};
