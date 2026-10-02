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
#include <memory>

#include "Application/Application.h"
#include "Document/Project.h"
#include "Document/Components/ComponentInstance.h"
#include "Document/Components/ComponentBlueprint.h"
#include "Telemetry/TelemetryRegistry.h"
#include "Telemetry/Sources/BodyMotionSource.h"
#include "Utils/Log.h"
#include <QJsonArray>

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
    // for (int channelId : targetRows.keys()) {
    //     if (auto ch = registry.getVector(channelId)) ch->unsubscribe();
    //     registry.removeSource(channelId);
    // }
    registry.clearAll();
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
        if (!comp || !comp->getBlueprint()) return;
        for (const QString& nodeId : comp->getBlueprint()->kinematics.getNodes().keys()) {
            bodyCombo->addItem(nodeId);
        }
    };
    fillBodies();
    connect(compCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, fillBodies);

    layout->addWidget(new QLabel("Quantity:"));
    QComboBox* quantityCombo = new QComboBox();
    quantityCombo->addItem("Linear Velocity (m/s)", QVariant::fromValue((int)BodyMotionSource::MotionType::LinearVelocity));
    quantityCombo->addItem("Angular Velocity (rad/s)", QVariant::fromValue((int)BodyMotionSource::MotionType::AngularVelocity));
    quantityCombo->addItem("Linear Acceleration (m/s²)", QVariant::fromValue((int)BodyMotionSource::MotionType::LinearAcceleration));
    quantityCombo->addItem("Angular Acceleration (rad/s²)", QVariant::fromValue((int)BodyMotionSource::MotionType::AngularAcceleration));
    layout->addWidget(quantityCombo);

    QCheckBox* localFrameCheck = new QCheckBox("Local frame (default: world)");
    layout->addWidget(localFrameCheck);

    layout->addStretch();

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted || bodyCombo->currentText().isEmpty()) return;

    addMotionTarget(compCombo->currentData().toInt(),
                    bodyCombo->currentText(),
                    quantityCombo->currentData().toInt(),
                    localFrameCheck->isChecked());
}




#pragma region Targets

void MotionPanel::addMotionTarget(int comp_uid, const QString& bodyName, int motionType, bool locality, const QColor& color) {
    auto& registry = TelemetryRegistry::getInstance();
    auto source = registry.getOrCreateSource(
        QString("motion:%1:%2:%3:%4").arg(comp_uid).arg(bodyName).arg(motionType).arg(locality?"local":"global"),
        [comp_uid, bodyName, motionType, locality]() {
            return std::make_shared<BodyMotionSource>(comp_uid, bodyName, (BodyMotionSource::MotionType)motionType, locality);
        }
    );
    QString sourceKey = source->getKey();
    if (targetRows.contains(sourceKey)) return;   // already plotted
    source->subscribe();

    plotWindow->addTarget(sourceKey, source->description, color);
    targetDescs.insert(sourceKey, {comp_uid, bodyName, motionType, locality});

    // Row UI: label + remove button
    QWidget* row = new QWidget();
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);

    QLabel* textLabel = new QLabel("↳ " + source->description);
    QPushButton* removeBtn = new QPushButton("✖");
    removeBtn->setFixedSize(24, 24);
    removeBtn->setStyleSheet("color: #E53935; border: none; font-weight: bold; font-size: 14px; background: transparent;");

    rowLayout->addWidget(textLabel);
    rowLayout->addStretch();
    rowLayout->addWidget(removeBtn);

    targetsLayout->addWidget(row);
    targetRows.insert(sourceKey, row);

    connect(removeBtn, &QPushButton::clicked, this, [this, sourceKey]() {
        removeMotionTarget(sourceKey);
    });
}




void MotionPanel::removeMotionTarget(const QString& sourceKey) {
    auto& registry = TelemetryRegistry::getInstance();
    if (auto ch = registry.getSource(sourceKey)){
        ch->unsubscribe();
        registry.clearInactive();
    }
    plotWindow->removeTarget(sourceKey);
    targetDescs.remove(sourceKey);
    if (QWidget* row = targetRows.take(sourceKey)) row->deleteLater();
}




#pragma region Persistence

static const char* kQuantityNames[] = {"linear_velocity", "angular_velocity", "linear_acceleration", "angular_acceleration"};

QJsonObject MotionPanel::toJson() const {
    QJsonArray targets;
    for (auto it = targetDescs.constBegin(); it != targetDescs.constEnd(); ++it) {
        const MotionTargetDesc& d = it.value();
        QJsonObject t;
        t["uid"] = d.uid;
        t["body"] = d.body;
        t["quantity"] = kQuantityNames[qBound(0, d.quantity, 3)];
        t["local"] = d.local;
        QColor c = plotWindow->targetColor(it.key());
        if (c.isValid()) t["color"] = c.name();
        targets.append(t);
    }
    return QJsonObject{{"targets", targets}};
}




void MotionPanel::loadJson(const QJsonObject& obj) {
    for (const QString& key : targetRows.keys()) removeMotionTarget(key);

    Project* project = Application::getInstance()->getProject();
    for (const QJsonValue& v : obj["targets"].toArray()) {
        QJsonObject t = v.toObject();
        const int uid = t["uid"].toInt();
        const QString body = t["body"].toString();

        int quantity = -1;
        for (int i = 0; i < 4; ++i)
            if (t["quantity"].toString() == kQuantityNames[i]) quantity = i;

        ComponentInstance* comp = project->getComponentByUid(uid);
        if (!comp || !comp->getBlueprint() || quantity < 0
            || !comp->getBlueprint()->kinematics.getNodes().contains(body)) {
            Log::warning(QString("Plotting: skipped motion target '%1' of component %2 (no longer exists)").arg(body).arg(uid));
            continue;
        }
        addMotionTarget(uid, body, quantity, t["local"].toBool(false), QColor(t["color"].toString()));
    }
}
