// src/View/Windows/ComponentEditorWindow.h
#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include "Document/Components/ComponentData.h"
#include "View/Viewport/ComponentPreviewViewport.h"

class KeyValueListWidget;



/*
 * Standalone window for creating/editing a component (.rsdef). Works
 * directly against ComponentData -- the same struct ComponentBlueprint
 * inherits from for cached catalog entries -- rather than a separate
 * editor-only draft type, so there's exactly one shape of "component data"
 * in the codebase, not two that could drift apart.
 *
 * Not tied to any open Project -- operates only on the models/ catalog via
 * LibraryManager, so it has no dependency on Application::currentProject.
 *
 * sourcePath/overwriteInPlace/isNewCatalogEntry stay as members of THIS
 * window, not fields on ComponentData -- they're properties of an editing
 * session, meaningless for a cached ComponentBlueprint sitting in the
 * catalog.
 *
 *   overwriteInPlace = true  -> rsdefPath is an existing cataloged
 *                                component; Save writes back to it, no
 *                                catalog change.
 *   overwriteInPlace = false -> rsdefPath is a TEMPLATE to seed initial
 *                                field values; modelId/meta.name are reset
 *                                after loading, and Save always goes to
 *                                custom/<id>/<id>.rsdef under a new
 *                                "Custom Components" catalog entry.
 */
class ComponentEditorWindow : public QWidget {
    Q_OBJECT

public:
    explicit ComponentEditorWindow(ComponentData source, QWidget* parent = nullptr);

private:
    ComponentData data;
    bool lockedComponent = false;
    QMap<QString, bool> bodyGeomOverride; // Tracks the state of mass/inertia override checkboxes

    struct TransformFieldRefs { QLineEdit* posEdit; QLineEdit* rotEdit; };
    TransformFieldRefs addTransformFields(QFormLayout* form, const Transform& t);
    QWidget* makeCollapsible(const QString& headerText, QWidget* content, QWidget* parent);

    QVBoxLayout* leftLayout;
    ComponentPreviewViewport* preview;
    QTimer* previewReloadTimer; // debounce -- regenerating MJCF per keystroke is wasteful

    // new members
    QTabWidget* tabWidget;
    QVBoxLayout* metaLayout;
    QVBoxLayout* constructionLayout;
    QVBoxLayout* ioLayout;
    QVBoxLayout* emulatorLayout;
    QSet<QString> foldedSections;

    // new methods
    bool eventFilter(QObject* obj, QEvent* event) override;
    void installScrollGuards(QWidget* root);

    // Debounced preview refresh: structural edits come through
    // clearAndRebuild(), field edits through their commit lambdas.
    void schedulePreviewReload();

    void onSaveClicked();

    // ---- section builders (construct + wire per section) ----
    void build_resources();
    void build_resource_list(QVBoxLayout* parent, const QString& title, QMap<QString, QString>& target);
    void build_construction();
    void build_bodies(QVBoxLayout* parent);
    void build_geoms(QFormLayout* bodyForm, const QString& bodyId);
    void build_sites(QFormLayout* bodyForm, const QString& bodyId);
    void build_joints(QVBoxLayout* parent);
    void build_connectors();
    void build_io();
    void build_io_list(QVBoxLayout* parent, const QString& title, QMap<QString, IODef>& target);
    void build_emulator();
    void build_data();

    void refreshBodyDropdown(QComboBox* combo);
    void refreshJointDropdown(QComboBox* combo);
    void refreshSiteDropdown(QComboBox* combo);

    void clearAndRebuild();
};
