#pragma once

#include "Rendering/OffscreenSim.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QTimer>
#include <QLabel>

class MViewport : public QLabel {
    Q_OBJECT

    private:
        QTimer timer;
        OffscreenSim *sim;
        
    private slots:
        void renderLoop();
        
    protected:
        void resizeEvent(QResizeEvent* event) override;

    public:
        explicit MViewport(QWidget* parent = nullptr);
        ~MViewport();

};