#pragma once

#include <QLabel>
#include <QTimer>
#include <QPoint>
#include "Simulation/MujocoContext.h"
#include "View/Viewport/OffscreenSim.h"
#include "View/Viewport/Camera.h"
#include "Document/Components/ComponentData.h"



/*
 * Standalone 3D preview for the ComponentEditorWindow. Renders the
 * component currently being edited -- nothing else.
 *
 * Owns its OWN MujocoContext (not the app's): the app context is the live
 * project simulation, so loading preview models into it would destroy the
 * running sim. Paired with its own OffscreenSim (own hidden GLFW window /
 * GL context), the preview is fully isolated and needs no physicsMutex --
 * everything here happens on the GUI thread.
 *
 * Static pose only: mj_forward after load, no stepping.
 */
class ComponentPreviewViewport : public QLabel {
    Q_OBJECT

    private:
        MujocoContext mujocoContext;
        OffscreenSim* sim;
        Camera* camera;
        QTimer timer;

        // Shown when there's nothing valid to render (empty component or
        // the last reload failed -- in which case the previous good frame
        // stays on screen and this is drawn as a corner warning).
        QString statusMessage;

        // Orbit state (spherical coords around target)
        QPoint lastMousePos;
        bool orbiting = false;
        bool panning = false;
        double distance = 0.25;
        double azimuthDeg = -90.0;
        double elevationDeg = 20.0;
        Position target{0, 0, 0};

        void applyCamera();
        void renderLoop();

    protected:
        void resizeEvent(QResizeEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;
        void paintEvent(QPaintEvent* event) override;

    public:
        explicit ComponentPreviewViewport(QWidget* parent = nullptr);
        ~ComponentPreviewViewport();

        // Regenerates MJCF from data and reloads the preview model.
        // On failure the previous model stays on screen and statusMessage
        // is set; safe to call with invalid mid-edit states.
        void loadComponentData(const ComponentData& data);
};
