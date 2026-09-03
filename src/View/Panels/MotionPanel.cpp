// src/View/Panels/MotionPanel.cpp
#include "MotionPanel.h"

#include <QScrollArea>
#include <QPushButton>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QFrame>

#include "Application/Application.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Telemetry/BodyMotionSource.h"

#pragma region Setup

MotionPanel::MotionPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(5, 5, 5, 5);
    outerLayout->setSpacing(8);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* scrollContent = new QWidget();
    targetsLayout = new QVBoxLayout(scrollContent);
    targetsLayout->setContentsMargins(0, 0, 0, 0);
    targetsLayout->setSpacing(4);
    targetsLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea);

    QHBoxLayout* actionsLayout = new QHBoxLayout();
    QPushButton* addTargetBtn = new QPushButton("+ Add Target");
    QPushButton* viewBtn = new QPushButton("View Plot");
    addTargetBtn->setMinimumHeight(35);
    viewBtn->setMinimumHeight(35);
    actionsLayout->addWidget(addTargetBtn);
    actionsLayout->addWidget(viewBtn);
    outerLayout->addLayout(actionsLayout);

    plotWindow = new VectorCanvasWindow("Body Motion", nullptr);

    connect(addTargetBtn, &QPushButton::clicked, this, &MotionPanel::showAddTargetDialog);
    connect(viewBtn, &QPushButton::clicked, this, [this]() {
        plotWindow->show();
        plotWindow->raise();
        plotWindow->activateWindow();
    });
}




MotionPanel::~MotionPanel() {
    auto& registry = TelemetryRegistry::getInstance();
    for (int channelId : targetRows.keys()) {
        if (auto ch = registry.getVector(channelId)) ch->unsubscribe();
        registry.removeSource(channelId);
    }
    if (plotWindow) plotWindow->deleteLater();
}

#pragma region Add Target

/*
 * Lets the user pick a component, one of its physical bodies, and a motion
 * quantity, then creates a BodyMotionSource feeding the shared plot window.
 */
void MotionPanel::showAddTargetDialog() {
    Project* project = Application::getInstance()->getProject();
    if (!project) return;

    auto& componentMap = project->getComponentMap();
    if (componentMap.isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Add Body Motion Target");
    dialog.setMinimumSize(350, 200);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel("Component:"));
    QComboBox* compCombo = new QComboBox();
    for (auto it = componentMap.begin(); it != componentMap.end(); ++it) {
        compCombo->addItem(QString("%1: %2").arg(it.key()).arg(it.value()->name), it.key());
    }
    layout->addWidget(compCombo);

    layout->addWidget(new QLabel("Body:"));
    QComboBox* bodyCombo = new QComboBox();
    layout->addWidget(bodyCombo);

    auto fillBodies = [&componentMap, compCombo, bodyCombo]() {
        bodyCombo->clear();
        ComponentInstance* comp = componentMap.value(compCombo->currentData().toInt(), nullptr);
        if (!comp || !comp->blueprint) return;
        for (const QString& nodeId : comp->blueprint->kinematics.getNodes().keys()) {
            bodyCombo->addItem(nodeId);
        }
    };
    fillBodies();
    connect(compCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, fillBodies);

    layout->addWidget(new QLabel("Quantity:"));
    QComboBox* quantityCombo = new QComboBox();
    quantityCombo->addItem("Linear Velocity (m/s)", QVariant::fromValue((int)BodyMotionSource::Quantity::LinearVelocity));
    quantityCombo->addItem("Angular Velocity (rad/s)", QVariant::fromValue((int)BodyMotionSource::Quantity::AngularVelocity));
    quantityCombo->addItem("Linear Acceleration (m/s²)", QVariant::fromValue((int)BodyMotionSource::Quantity::LinearAcceleration));
    quantityCombo->addItem("Angular Acceleration (rad/s²)", QVariant::fromValue((int)BodyMotionSource::Quantity::AngularAcceleration));
    layout->addWidget(quantityCombo);

    QCheckBox* localFrameCheck = new QCheckBox("Local frame (default: world)");
    layout->addWidget(localFrameCheck);

    layout->addStretch();

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted || bodyCombo->currentText().isEmpty()) return;

    auto source = std::make_shared<BodyMotionSource>(
        compCombo->currentData().toInt(),
        bodyCombo->currentText(),
        static_cast<BodyMotionSource::Quantity>(quantityCombo->currentData().toInt()),
        localFrameCheck->isChecked());

    auto& registry = TelemetryRegistry::getInstance();
    registry.addSource(source);

    int channelId = source->channelId();
    if (auto ch = registry.getVector(channelId)) ch->subscribe();
    plotWindow->addTarget(channelId, source->description());

    // Row UI: label + remove button
    QWidget* row = new QWidget();
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);

    QLabel* textLabel = new QLabel("↳ " + source->description());
    QPushButton* removeBtn = new QPushButton("✖");
    removeBtn->setFixedSize(24, 24);
    removeBtn->setStyleSheet("color: #E53935; border: none; font-weight: bold; font-size: 14px; background: transparent;");

    rowLayout->addWidget(textLabel);
    rowLayout->addStretch();
    rowLayout->addWidget(removeBtn);

    targetsLayout->addWidget(row);
    targetRows.insert(channelId, row);

    connect(removeBtn, &QPushButton::clicked, this, [this, row, channelId]() {
        auto& registry = TelemetryRegistry::getInstance();
        if (auto ch = registry.getVector(channelId)) ch->unsubscribe();
        plotWindow->removeTarget(channelId);
        registry.removeSource(channelId);
        targetRows.remove(channelId);
        row->deleteLater();
    });
}
