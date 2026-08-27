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

        QString dir;
        QMap<QString, ComponentBlueprint*> blueprints;
        QList<CategoryDef> categories;

        static const QString CUSTOM_CATEGORY_ID;
        static const QString CUSTOM_CATEGORY_NAME;

        void appendCustomCatalogEntry(const QString& relativeItemPath, const QString& modelId);
        QString copyResourceFile(const QString& srcAbsPath, const QString& destDir, const QString& subfolder);


    public:
        static LibraryManager& getInstance();

        void fetchOnline();
        bool load(const QString& models_dir="");
        bool saveComponent(ComponentData draft, bool isNewCatalogEntry);

        QString getModelsDir();
        ComponentBlueprint* getBlueprint(const QString& model_id);
        const QList<CategoryDef>& getCategories() const { return categories; }

    signals:
        void catalogLoaded();
        void componentSaved(const QString& modelId);
};
