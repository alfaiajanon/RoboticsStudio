#include "InspectorPanel.h"
#include "Simulation/Components/ComponentInstance.h"
#include "Simulation/Components/ComponentBlueprint.h"
#include "Core/Application.h" 





/*
 * Initializes the inspector panel layout.
 * Sets the default visual state to empty.
 */
InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent), currentUid(-1) {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignTop);
    showEmptyState();
}





/*
 * Recursively deletes all widgets and layouts within a given layout.
 * Ensures clean memory management when rebuilding the UI.
 */
void InspectorPanel::clearLayout(QLayout* layout) {
    if (!layout) return;
    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        } else if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        delete item;
    }
}





/*
 * Renders a placeholder label.
 * Displayed when no component is currently selected in the editor.
 */
void InspectorPanel::showEmptyState() {
    clearLayout(mainLayout);
    
    QLabel* emptyLabel = new QLabel("No Component Selected", this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet("color: gray; font-style: italic;");
    
    mainLayout->addWidget(emptyLabel);
}





/*
 * Updates the active component tracking ID.
 * Triggers a full UI rebuild if the selection has changed.
 */
void InspectorPanel::setComponent(int uid) {
    if (currentUid == uid) return; 
    
    currentUid = uid;
    buildUI();
}





/*
 * Dynamically constructs the inspector UI based on the active component's blueprint.
 * Generates data-bound sliders for inputs and read-only labels for sensor outputs.
 */
void InspectorPanel::buildUI() {
    clearLayout(mainLayout);

    if (currentUid == -1) {
        showEmptyState();
        return;
    }

    Project* project = Application::getInstance()->getProject();
    ComponentInstance* comp = project->getComponentByUid(currentUid);

    if (!comp || !comp->blueprint) {
        showEmptyState();
        return;
    }

    QGroupBox* infoBox = new QGroupBox("Component Info", this);
    QFormLayout* infoLayout = new QFormLayout(infoBox);
    infoLayout->addRow("Name:", new QLabel(comp->name, infoBox));
    infoLayout->addRow("Model:", new QLabel(comp->model, infoBox));
    mainLayout->addWidget(infoBox);

    if (!comp->blueprint->inputDefs.isEmpty()) {
        QGroupBox* inputBox = new QGroupBox("Inputs & Controls", this);
        QFormLayout* inputLayout = new QFormLayout(inputBox);

        for (const QString& key : comp->blueprint->inputDefs.keys()) {
            IODef def = comp->blueprint->inputDefs[key];

            QWidget* targetWidget = new QWidget(inputBox); 
            QHBoxLayout* targetHBox = new QHBoxLayout(targetWidget);
            targetHBox->setContentsMargins(0, 0, 0, 0);

            QSlider* slider = new QSlider(Qt::Horizontal, targetWidget);
            slider->setRange(def.range.first, def.range.second);
            slider->setValue(comp->getInputTarget(key));

            QDoubleSpinBox* spinBox = new QDoubleSpinBox(targetWidget);
            spinBox->setRange(def.range.first, def.range.second);
            spinBox->setValue(comp->getInputTarget(key));

            connect(slider, &QSlider::valueChanged, spinBox, [spinBox](int val){ spinBox->setValue(val); });
            connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), slider, [slider](double val){ slider->setValue(val); });

            int capturedUid = currentUid;
            connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [capturedUid, key](double val) {
                Project* project = Application::getInstance()->getProject();
                ComponentInstance* activeComp = project->getComponentByUid(capturedUid);
                if (activeComp) {
                    if (activeComp) activeComp->setInputTarget(key, val);
                }
            });

            targetHBox->addWidget(slider);
            targetHBox->addWidget(spinBox);
            inputLayout->addRow(def.name + ":", targetWidget);
        }
        mainLayout->addWidget(inputBox);
    }

    if (!comp->blueprint->outputDefs.isEmpty()) {
        QGroupBox* outputBox = new QGroupBox("Sensor Outputs", this);
        QFormLayout* outputLayout = new QFormLayout(outputBox);

        for (const QString& key : comp->blueprint->outputDefs.keys()) {
            IODef def = comp->blueprint->outputDefs[key];
            
            QLabel* valueLabel = new QLabel(QString::number(comp->getOutputCurrent(key)), outputBox);
            valueLabel->setObjectName("lbl_out_" + key); 

            outputLayout->addRow(def.name + " (" + def.unit + "):", valueLabel);
        }
        mainLayout->addWidget(outputBox);
    }

    mainLayout->addStretch(); 
}





/*
 * Refreshes all read-only sensor labels with current telemetry data.
 * Designed to be called safely at high frequencies by the physics loop timer.
 */
void InspectorPanel::updateLiveValues() {
    if (currentUid == -1) return;

    ComponentInstance* comp = Application::getInstance()->getProject()->getComponentByUid(currentUid);
    
    if (!comp) return;

    for (const QString& key : comp->blueprint->outputDefs.keys()) {
        QLabel* label = this->findChild<QLabel*>("lbl_out_" + key);
        if (label) {
            double val = comp->getOutputCurrent(key);
            label->setText(QString::number(val, 'f', 2)); 
        }
    }
}