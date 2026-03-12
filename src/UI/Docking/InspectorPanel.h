#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>

class InspectorPanel : public QWidget {
    Q_OBJECT

private:
    int currentUid = -1;
    QVBoxLayout* mainLayout;
    
    void clearLayout(QLayout* layout); 
    void showEmptyState();
    void buildUI();

public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    void setComponent(int uid);
    void updateLiveValues(); 
};