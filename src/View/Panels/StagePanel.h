#pragma once

#include <functional>
#include <QHash>
#include <QWidget>
#include "Document/Stage.h"

class QFormLayout;
class QListWidget;
class QLabel;


/*
 * Bottom-tab editor for the project's Stage: environment, floor, props and
 * the stage script. Every edit goes through the undo stack as a whole-stage
 * snapshot (GenericCommand), then reloads the simulation. refresh() pulls
 * the widgets back from the Stage (after undo/redo or a project load).
 */
class StagePanel : public QWidget {
    Q_OBJECT

    public:
        explicit StagePanel(QWidget* parent = nullptr);

    public slots:
        void refresh();

    protected:
        bool eventFilter(QObject* obj, QEvent* event) override;

    private:
        template <typename T> using Field = std::function<T*(Stage::Data&)>;

        QList<std::function<void(Stage::Data&)>> loaders;   // widget <- data, run by refresh()
        bool updating = false;
        int currentProp = -1;

        QListWidget* propList = nullptr;
        QWidget* propEditor = nullptr;
        QFormLayout* envForm = nullptr;
        QFormLayout* floorForm = nullptr;
        QFormLayout* propForm = nullptr;
        QLabel* scriptLabel = nullptr;

        void setupUI();
        QWidget* buildEnvironment();
        QWidget* buildFloor();
        QWidget* buildProps();
        QWidget* buildToolbar();

        // Applies `edit` to a copy of the stage data and pushes it as one undo
        // step; consecutive edits with the same non-empty mergeKey collapse.
        void commit(const std::function<void(Stage::Data&)>& edit, const QString& mergeKey = "");

        // Field binders: add a row to `form` and keep it in sync with the data.
        QWidget* addDouble(QFormLayout* form, const QString& label, Field<double> field,
                           double min, double max, double step, int decimals, const QString& key);
        QWidget* addVec3(QFormLayout* form, const QString& label, Field<Vec3> field,
                         double min, double max, double step, int decimals, const QString& key);
        QWidget* addColor(QFormLayout* form, const QString& label, Field<Vec3> field);
        QWidget* addColor(QFormLayout* form, const QString& label, Field<Vec4> field);
        QWidget* addBool(QFormLayout* form, const QString& label, Field<bool> field);
        QWidget* addInt(QFormLayout* form, const QString& label, Field<int> field, int min, int max, const QString& key);
        QWidget* addChoice(QFormLayout* form, const QString& label, Field<QString> field, const QStringList& choices);
        QWidget* addFile(QFormLayout* form, const QString& label, Field<QString> field, const QString& filter);

        QHash<QString, QWidget*> rows;   // rows whose visibility depends on other fields
        Stage::Prop* selectedProp(Stage::Data& d);
        void updateVisibility(const Stage::Data& d);
        QString importIntoProject(const QString& path);   // relative path inside the project, "" on failure
        void editStageScript();
};
