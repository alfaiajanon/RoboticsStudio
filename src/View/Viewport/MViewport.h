#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QTimer>
#include <QLabel>
#include "OffscreenSim.h"



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