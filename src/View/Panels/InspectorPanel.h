#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QString>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>




class ComponentInstance;
class Project;



class ConnectorDropTargetBtn : public QPushButton {
    Q_OBJECT

    public:
        explicit ConnectorDropTargetBtn(const QString& connectorId, const QString& text, QWidget* parent = nullptr);

    signals:
        void componentDropped(const QString& connectorId, const QString& modelId);

    protected:
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dragMoveEvent(QDragMoveEvent* event) override;
        void dragLeaveEvent(QDragLeaveEvent* event) override;
        void dropEvent(QDropEvent* event) override;

    private:
        QString connectorId;
};








class InspectorPanel : public QWidget {
    Q_OBJECT

    private:
        int currentUid = -1;
        QVBoxLayout* mainLayout;
        QWidget* contentWidget;

        // Set while a joint slider is being mouse-dragged: buildUI() defers
        // rebuilds so the drag doesn't lose focus mid-gesture, and joint
        // values are previewed directly on the document until sliderRelease
        // commits a single undoable command.
        bool dragInProgress = false;
        // Joint value captured on sliderPressed; the release command undoes to this.
        double dragStartValue = 0.0;
        // Each slider drag gets a unique merge key (see below), so a finished
        // drag can never merge with the NEXT edit -- every drag is its own
        // undo step. Keyboard/spinbox nudges share a per-field "nudge" key and
        // only merge with immediately consecutive nudges.
        int dragSessionCounter = 0;
        QString dragMergeKey;
        // Set when a buildUI() request was suppressed by dragInProgress;
        // sliderReleased runs the deferred rebuild.
        bool rebuildPending = false;
        
        void clearLayout(QLayout* layout); 
        void showEmptyState();

        void build_UID_0();
        void build_inputs(ComponentInstance* comp);
        void build_outputs(ComponentInstance* comp);

        void build_connectors(ComponentInstance* comp);
        void build_globalSettings(Project* project);
        void build_rootAttachment(Project* project);
        
        void populateAvailableComponents(QComboBox* combo);
        void populateAvailableConnectors(QComboBox* combo, int targetUid);
        void pushCommand(std::function<void()> doFn, 
                    std::function<void()> undoFn,
                    const QString& text = "", 
                    const QString& mergeKey = "",
                    bool requiresReload = true);
        
    public:
        explicit InspectorPanel(QWidget* parent = nullptr);
        void buildUI();

        void setComponent(int uid);
        void setInputState(bool flag);

        void updateLiveValues();
        // Lightweight alternative to buildUI() for undoing value-only
        // commands: syncs the joint sliders/spinboxes from the document
        // without rebuilding (and de-focusing) the panel.
        void updateJointValues();

    protected:
        bool eventFilter(QObject* obj, QEvent* event) override;
};