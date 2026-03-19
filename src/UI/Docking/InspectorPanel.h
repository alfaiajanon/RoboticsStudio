#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

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
        
        void clearLayout(QLayout* layout); 
        void showEmptyState();
        
        void populateAvailableComponents(QComboBox* combo);
        void populateAvailableConnectors(QComboBox* combo, int targetUid);
        
    public:
        explicit InspectorPanel(QWidget* parent = nullptr);
        void buildUI();

        void setComponent(int uid);
        void setInputState(bool flag);

        void updateLiveValues(); 

    protected:
        bool eventFilter(QObject* obj, QEvent* event) override;
};