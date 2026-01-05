#include "MViewport.h"
#include <QPainter>
#include <QSurfaceFormat>
#include <QResizeEvent> 
#include <QDebug>

MViewport::MViewport(QWidget *parent) : QLabel(parent){
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setMinimumSize(1, 1);

    sim = new OffscreenSim();
    sim->init(width(), height());
    connect(&timer, &QTimer::timeout, this, &MViewport::renderLoop);
    timer.start(16);
}

MViewport::~MViewport(){
}


void MViewport::resizeEvent(QResizeEvent *event){
    sim->setSize(
        event->size().width(), 
        event->size().height()
    );
}

void MViewport::renderLoop(){
    MujocoContext* mujocoContext = MujocoContext::getInstance();
    mujocoContext->step();
    mujocoContext->step();

    QImage currentFrame = sim->render();
    setPixmap(QPixmap::fromImage(currentFrame));

}
