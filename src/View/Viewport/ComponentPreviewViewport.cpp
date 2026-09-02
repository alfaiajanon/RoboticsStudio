#include "ComponentPreviewViewport.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QtMath>

#include "Document/Components/ComponentBlueprint.h"




ComponentPreviewViewport::ComponentPreviewViewport(QWidget* parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setMinimumSize(200, 200);
    setMouseTracking(false);

    sim = new OffscreenSim(&mujocoContext);
    sim->init(width(), height());

    camera = new Camera(mujocoContext.getCamera());
    applyCamera();

    connect(&timer, &QTimer::timeout, this, &ComponentPreviewViewport::renderLoop);
    timer.start(16);
}




ComponentPreviewViewport::~ComponentPreviewViewport() {
    timer.stop();
    delete sim;
    delete camera;
}




void ComponentPreviewViewport::resizeEvent(QResizeEvent* event) {
    sim->setSize(event->size().width(), event->size().height());
}




/*
 * Builds a standalone MJCF document for the component (edit-mode styling:
 * skybox gradient + one directional light, no floor plane) and swaps it
 * into the preview context. Mirrors Project::generateMujocoXML's
 * edit-mode branch, minus everything assembly-related.
 */
void ComponentPreviewViewport::loadComponentData(const ComponentData& data) {
    if (data.bodies.isEmpty() || !data.bodies.contains(data.defaultBodyId)) {
        statusMessage = "Add a body and set a default body to preview.";
        return;
    }

    ComponentBlueprint blueprint(data);

    QString xml = QString(
        "<mujoco model=\"%1_preview\">\n"
        "  <asset>\n"
        "    <texture type=\"skybox\" builtin=\"gradient\" rgb1=\"0.07 0.08 0.10\" rgb2=\"0.06 0.065 0.07\" width=\"512\" height=\"512\"/>\n"
        "%2"
        "  </asset>\n\n"
        "  <worldbody>\n"
        "    <light directional=\"true\" diffuse=\"0.6 0.6 0.6\" specular=\"0.2 0.2 0.2\" pos=\"-0.5 -.5 .5\" dir=\"1 1 -1\"/>\n"
        "%3"
        "  </worldbody>\n\n"
        "  <contact>\n%4  </contact>\n\n"
        "  <actuator>\n%5  </actuator>\n\n"
        "  <sensor>\n%6  </sensor>\n"
        "</mujoco>\n")
        .arg(blueprint.getModelId().isEmpty() ? QString("component") : blueprint.getModelId())
        .arg(blueprint.getAssetXML())
        .arg(blueprint.generateTreeXML(0, QString(), Transform()))
        .arg(blueprint.generateContactsXML(0))
        .arg(blueprint.generateActuatorXML(0))
        .arg(blueprint.generateSensorXML(0));

    if (mujocoContext.loadModelFromString(xml.toStdString())) {
        statusMessage.clear();
    } else {
        statusMessage = "Preview failed to load -- fix the component definition.";
    }
}




void ComponentPreviewViewport::renderLoop() {
    if (!mujocoContext.getModel()) {
        update(); // no model -- paintEvent draws the status message
        return;
    }
    QImage frame = sim->render();
    if (!frame.isNull()) {
        setPixmap(QPixmap::fromImage(frame));
    }
}




void ComponentPreviewViewport::applyCamera() {
    double az = qDegreesToRadians(azimuthDeg);
    double el = qDegreesToRadians(elevationDeg);
    camera->setTarget(target);
    camera->setPosition(Position(
        target.x + distance * qCos(el) * qCos(az),
        target.y + distance * qCos(el) * qSin(az),
        target.z + distance * qSin(el)
    ));
}




void ComponentPreviewViewport::mousePressEvent(QMouseEvent* event) {
    lastMousePos = event->pos();
    if (event->button() == Qt::LeftButton) orbiting = true;
    if (event->button() == Qt::RightButton) panning = true;
}




void ComponentPreviewViewport::mouseMoveEvent(QMouseEvent* event) {
    QPoint delta = event->pos() - lastMousePos;
    lastMousePos = event->pos();

    // OffscreenSim flips the rendered frame vertically, which mirrors the
    // apparent left-right motion -- compensate by negating horizontal drag.
    int dx = -delta.x();
    int dy = delta.y();

    if (orbiting) {
        azimuthDeg += dx * 0.5;
        elevationDeg = qBound(-89.0, elevationDeg + dy * 0.5, 89.0);
        applyCamera();
    } else if (panning) {
        // Move the target in the camera's ground-plane frame
        double az = qDegreesToRadians(azimuthDeg);
        double scale = distance * 0.002;
        target.x -= dx * scale * -qSin(az) + dy * scale * qCos(az);
        target.y -= dx * scale * qCos(az) + dy * scale * qSin(az);
        applyCamera();
    }
}




void ComponentPreviewViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) orbiting = false;
    if (event->button() == Qt::RightButton) panning = false;
}




void ComponentPreviewViewport::wheelEvent(QWheelEvent* event) {
    distance *= qPow(0.9, event->angleDelta().y() / 120.0);
    distance = qBound(0.01, distance, 10.0);
    applyCamera();
}




void ComponentPreviewViewport::paintEvent(QPaintEvent* event) {
    if (!mujocoContext.getModel()) {
        // No valid model yet -- draw the backdrop + status ourselves.
        QPainter p(this);
        p.fillRect(rect(), QColor(26, 26, 26));
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, statusMessage.isEmpty() ? "No preview" : statusMessage);
        return;
    }

    QLabel::paintEvent(event); // blit the rendered frame

    if (!statusMessage.isEmpty()) {
        // Last reload failed; the previous frame is still up -- warn in the corner.
        QPainter p(this);
        p.setPen(QColor(255, 120, 120));
        p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignTop | Qt::AlignLeft, statusMessage);
    }
}
