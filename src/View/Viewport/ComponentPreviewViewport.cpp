#include "ComponentPreviewViewport.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QtMath>
#include <QFrame>
#include <QCheckBox>
#include <QVBoxLayout>
#include <qcombobox.h>
#include <qpushbutton.h>

#include "Document/Components/ComponentBlueprint.h"



#pragma region Overlay helpers

namespace {

// World pose of a preview-model body (names are prefixed "comp_0_").
// Returns false when the body is absent from the currently loaded model
// (e.g. stale model after a failed mid-edit reload).
bool bodyWorldPose(const mjModel* m, const mjData* d, const QString& bodyId,
                   Position& pos, Rotation& rot) {
    QByteArray name = ("comp_0_" + bodyId).toUtf8();
    int id = mj_name2id(m, mjOBJ_BODY, name.constData());
    if (id < 0) return false;
    pos = Position(d->xpos[3 * id], d->xpos[3 * id + 1], d->xpos[3 * id + 2]);
    rot = Rotation(d->xquat[4 * id], d->xquat[4 * id + 1], d->xquat[4 * id + 2], d->xquat[4 * id + 3]);
    return true;
}

void addDecorGeom(mjvScene* scn, const mjvGeom& geom) {
    if (scn->ngeom < scn->maxgeom) scn->geoms[scn->ngeom++] = geom;
}

// Connector-type geom (line / arrow) between two world-space points.
// mjGEOM_LINE width is in pixels; mjGEOM_ARROW width is in meters.
void addConnectorGeom(mjvScene* scn, int type, double width,
                      const Position& a, const Position& b, const float rgba[4]) {
    mjvGeom geom;
    mjv_initGeom(&geom, type, nullptr, nullptr, nullptr, rgba);
    mjtNum from[3] = {a.x, a.y, a.z};
    mjtNum to[3] = {b.x, b.y, b.z};
    mjv_connector(&geom, type, width, from, to);
    addDecorGeom(scn, geom);
}

void addSphereGeom(mjvScene* scn, const Position& center, double radius, const float rgba[4]) {
    mjvGeom geom;
    mjtNum size[3] = {radius, 0.0, 0.0};
    mjtNum pos[3] = {center.x, center.y, center.z};
    mjv_initGeom(&geom, mjGEOM_SPHERE, size, pos, nullptr, rgba);
    addDecorGeom(scn, geom);
}

} // namespace

#pragma endregion




ComponentPreviewViewport::ComponentPreviewViewport(QWidget* parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setMinimumSize(200, 200);
    setMouseTracking(false);

    sim = new OffscreenSim(&mujocoContext);
    sim->init(width(), height());
    sim->decorHook = [this](mjModel* m, mjData* d, mjvScene* scn) { drawOverlays(m, d, scn); };

    camera = new Camera(mujocoContext.getCamera());
    applyCamera();

    // Floating HUD: visibility toggles for the overlay layers.
    hud = new QFrame(this);
    hud->setObjectName("previewHud");
    hud->setStyleSheet(
        "QFrame#previewHud { background-color: rgba(20, 20, 24, 190);"
        " border: 1px solid rgba(255, 255, 255, 40); border-radius: 6px; }"
        "QCheckBox { color: #dddddd; background: transparent; spacing: 6px; }");
    QVBoxLayout* hudLayout = new QVBoxLayout(hud);
    hudLayout->setContentsMargins(10, 6, 10, 6);
    hudLayout->setSpacing(2);

    QCheckBox* connectorsBox = new QCheckBox("Connectors", hud);
    connectorsBox->setChecked(showConnectors);
    QCheckBox* jointsBox = new QCheckBox("Joints", hud);
    jointsBox->setChecked(showJoints);
    hudLayout->addWidget(connectorsBox);
    hudLayout->addWidget(jointsBox);

    QCheckBox* orthoBox = new QCheckBox("Orthographic", hud);  // becomes this->orthoBox now
    orthoBox->setChecked(orthographic);
    hudLayout->addWidget(orthoBox);
    connect(orthoBox, &QCheckBox::toggled, this, [this](bool on) { setOrthographic(on); });
    this->orthoBox = orthoBox;

    QComboBox* viewCombo = new QComboBox(hud);
    viewCombo->addItem("Top", static_cast<int>(OrthoView::Top));
    viewCombo->addItem("Bottom", static_cast<int>(OrthoView::Bottom));
    viewCombo->addItem("Front", static_cast<int>(OrthoView::Front));
    viewCombo->addItem("Back", static_cast<int>(OrthoView::Back));
    viewCombo->addItem("Left", static_cast<int>(OrthoView::Left));
    viewCombo->addItem("Right", static_cast<int>(OrthoView::Right));
    viewCombo->setPlaceholderText("Snap view...");
    viewCombo->setCurrentIndex(-1);
    hudLayout->addWidget(viewCombo);

    connect(viewCombo, &QComboBox::activated, this, [this, viewCombo](int idx) {
        snapView(static_cast<OrthoView>(viewCombo->itemData(idx).toInt()));
    });
    connect(connectorsBox, &QCheckBox::toggled, this, [this](bool on) { showConnectors = on; });
    connect(jointsBox, &QCheckBox::toggled, this, [this](bool on) { showJoints = on; });

    hud->adjustSize();
    positionHud();
    hud->raise();
    hud->show();

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
    positionHud();
}




void ComponentPreviewViewport::positionHud() {
    if (hud) hud->move(width() - hud->width() - 8, 8);
}




/*
 * Builds a standalone MJCF document for the component (edit-mode styling:
 * skybox gradient + one directional light, no floor plane) and swaps it
 * into the preview context. Mirrors Project::generateMujocoXML's
 * edit-mode branch, minus everything assembly-related.
 */
void ComponentPreviewViewport::loadComponentData(const ComponentData& data) {
    currentData = data;

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
        "  <tendon>\n%7  </tendon>\n\n"
        "  <actuator>\n%5  </actuator>\n\n"
        "  <sensor>\n%6  </sensor>\n"
        "</mujoco>\n")
        .arg(blueprint.getModelId().isEmpty() ? QString("component") : blueprint.getModelId())
        .arg(blueprint.getAssetXML())
        .arg(blueprint.generateTreeXML(0, QString(), Transform()))
        .arg(blueprint.generateContactsXML(0))
        .arg(blueprint.generateActuatorXML(0))
        .arg(blueprint.generateBasicSensorXML(0))
        .arg(blueprint.generateTendonXML(0));

    if (mujocoContext.loadModelFromString(xml.toStdString())) {
        statusMessage.clear();
    } else {
        statusMessage = "Preview failed to load -- fix the component definition.";
    }

    if(orthographic){
        mujocoContext.getModel()->vis.global.orthographic=1;  // reapply it
        applyCamera();
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
    if(orthographic){
        mujocoContext.getModel()->vis.global.fovy=distance;
    }
}




void ComponentPreviewViewport::mousePressEvent(QMouseEvent* event) {
    lastMousePos = event->pos();

    if (event->button() == Qt::MiddleButton) {
        panning = true;
    } else if (event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ControlModifier) {
            panning = true;
        } else {
            orbiting = true;
        }
    }
}

void ComponentPreviewViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        orbiting = false;
        panning = false; // also clears the Ctrl+Left-drag pan case
    } else if (event->button() == Qt::MiddleButton) {
        panning = false;
    }
}


void ComponentPreviewViewport::mouseMoveEvent(QMouseEvent* event) {
    QPoint delta = event->pos() - lastMousePos;
    lastMousePos = event->pos();

    int dx = delta.x();
    int dy = delta.y();

    if (orbiting) {
        azimuthDeg -= dx * 0.5;
        elevationDeg = qBound(-89.0, elevationDeg + dy * 0.5, 89.0);
        applyCamera();
    } else if (panning) {
        double az = qDegreesToRadians(azimuthDeg);
        double el = qDegreesToRadians(elevationDeg);

        // Screen-space right/up in world coordinates, derived from the same
        // spherical az/el applyCamera() already uses -- "up" varies with
        // elevation on purpose: at el=90 (top-down) there is no vertical
        // component left to pan along, only horizontal, which is exactly
        // the case the old dy*cos/sin(az)-only formula got wrong.
        Position right(-qSin(az), qCos(az), 0.0);
        Position up(-qCos(az) * qSin(el), -qSin(az) * qSin(el), qCos(el));

        // Grab/hand-tool semantics: content follows the cursor, so target
        // moves opposite the screen-space drag direction along "right",
        // and dy (Qt: positive = downward) inverts against "up".
        double scale = distance * 0.002;
        target.x += (-dx * right.x + dy * up.x) * scale;
        target.y += (-dx * right.y + dy * up.y) * scale;
        target.z += (-dx * right.z + dy * up.z) * scale;

        applyCamera();
    }
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




#pragma region Overlays

/*
 * Appends the visualization overlays (decor geoms) to the scene, called
 * every frame by OffscreenSim between mjv_updateScene and mjr_render.
 *
 * Connector: 3-arrow axis frame at the connector pose (X red, Y green,
 * Z blue). Joint: hollow square in the joint frame's XY plane (joint axis
 * is Z) plus a line to each connected body's origin with a filled circle
 * at the body end.
 *
 * Marker world poses are composed from the EDITED data (currentData) and
 * the preview model's body poses: connectors/joints are stored in the
 * component root frame, so world = bodyWorld * (bodyLocal^-1 * transform).
 * Markers whose body is missing from the loaded model are skipped.
 */
void ComponentPreviewViewport::drawOverlays(mjModel* m, mjData* d, mjvScene* scn) {
    if (!m || !d || !scn) return;

    if (showConnectors) {
        const double axisLen = 0.02;    // 2 cm
        const float rgbaX[4] = {1.0f, 0.25f, 0.25f, 1.0f};
        const float rgbaY[4] = {0.25f, 1.0f, 0.25f, 1.0f};
        const float rgbaZ[4] = {0.35f, 0.55f, 1.0f, 1.0f};

        for (const ConnectorDef& conn : currentData.connectors) {
            if (!currentData.bodies.contains(conn.body)) continue;

            Position bodyPos;
            Rotation bodyRot;
            if (!bodyWorldPose(m, d, conn.body, bodyPos, bodyRot)) continue;

            Transform rel = currentData.bodies[conn.body].localTransform.inverse() * conn.transform;
            Transform world = Transform(bodyPos, bodyRot) * rel;

            addConnectorGeom(scn, mjGEOM_ARROW, 0.0015, world.position,
                             world.position + world.rotation.rotate(Position(axisLen, 0, 0)), rgbaX);
            addConnectorGeom(scn, mjGEOM_ARROW, 0.0015, world.position,
                             world.position + world.rotation.rotate(Position(0, axisLen, 0)), rgbaY);
            addConnectorGeom(scn, mjGEOM_ARROW, 0.0015, world.position,
                             world.position + world.rotation.rotate(Position(0, 0, axisLen)), rgbaZ);
        }
    }

    if (showJoints) {
        const double halfSize = 0.008;  // square half-extent, 8 mm
        const float rgbaJoint[4] = {1.0f, 0.7f, 0.15f, 1.0f};
        const float rgbaLink[4] = {1.0f, 0.7f, 0.15f, 0.8f};

        for (const Edge& edge : currentData.joints) {
            if (!currentData.bodies.contains(edge.bodyA) || !currentData.bodies.contains(edge.bodyB)) {
                continue;
            }

            Position posA, posB;
            Rotation rotA, rotB;
            if (!bodyWorldPose(m, d, edge.bodyA, posA, rotA)) continue;
            if (!bodyWorldPose(m, d, edge.bodyB, posB, rotB)) continue;

            Transform rel = currentData.bodies[edge.bodyA].localTransform.inverse() * edge.localTransform;
            Transform jointWorld = Transform(posA, rotA) * rel;

            // Hollow square in the joint frame's XY plane (joint axis = Z).
            Position corners[4] = {
                jointWorld.position + jointWorld.rotation.rotate(Position(halfSize, halfSize, 0)),
                jointWorld.position + jointWorld.rotation.rotate(Position(-halfSize, halfSize, 0)),
                jointWorld.position + jointWorld.rotation.rotate(Position(-halfSize, -halfSize, 0)),
                jointWorld.position + jointWorld.rotation.rotate(Position(halfSize, -halfSize, 0)),
            };
            for (int i = 0; i < 4; ++i) {
                addConnectorGeom(scn, mjGEOM_LINE, 2.0, corners[i], corners[(i + 1) % 4], rgbaJoint);
            }

            // Lines from the joint anchor to each body origin + filled
            // circle at each body end.
            addConnectorGeom(scn, mjGEOM_LINE, 1.5, jointWorld.position, posA, rgbaLink);
            addConnectorGeom(scn, mjGEOM_LINE, 1.5, jointWorld.position, posB, rgbaLink);
            addSphereGeom(scn, posA, 0.0025, rgbaJoint);
            addSphereGeom(scn, posB, 0.0025, rgbaJoint);
        }
    }
}

#pragma endregion





void ComponentPreviewViewport::setOrthographic(bool on) {
    orthographic = on;
    if (mujocoContext.getModel()) {
        mujocoContext.getModel()->vis.global.orthographic = on ? 1 : 0;
    }
    if (on) {
        // See note on wheelEvent below -- fovy means something different in
        // ortho mode and needs seeding here, not left at whatever MuJoCo's
        // compiled default is.
        mujocoContext.getModel()->vis.global.fovy=distance;
    }else{
        mujocoContext.getModel()->vis.global.fovy=45;
    }
    if (orthoBox) {
        orthoBox->blockSignals(true);
        orthoBox->setChecked(on);
        orthoBox->blockSignals(false);
    }
}

/*
 * Elevation ±90 is a controlled, deliberate snap (not a user drag), so
 * bypassing wheelEvent/mouseMoveEvent's [-89,89] clamp here is intentional
 * -- azimuth becomes mathematically irrelevant at the poles anyway
 * (cos(90)=0 kills its contribution in applyCamera's spherical formula).
 */
void ComponentPreviewViewport::snapView(OrthoView view) {
    switch (view) {
        case OrthoView::Top:    elevationDeg = 90.0;  break;
        case OrthoView::Bottom: elevationDeg = -90.0; break;
        case OrthoView::Right:  elevationDeg = 0.0; azimuthDeg = 0.0;   break;
        case OrthoView::Left:   elevationDeg = 0.0; azimuthDeg = 180.0; break;
        // GUESS: world +Y = "back", -Y = "front" -- nothing I've seen in
        // this codebase states a forward convention. Flip these two if
        // wrong; nothing else depends on this choice.
        case OrthoView::Front:  elevationDeg = 0.0; azimuthDeg = -90.0; break;
        case OrthoView::Back:   elevationDeg = 0.0; azimuthDeg = 90.0;  break;
    }
    setOrthographic(true);
    applyCamera();
}
