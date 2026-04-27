#include "MViewport.h"
#include <QPainter>
#include <QSurfaceFormat>
#include <QResizeEvent> 
#include <QDebug>
#include "Core/Application.h"
#include "Simulation/SimulationManager.h"





MViewport::MViewport(QWidget *parent) : QLabel(parent){
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setMinimumSize(1, 1);

    sim = new OffscreenSim();
    sim->init(width(), height());
    connect(&timer, &QTimer::timeout, this, &MViewport::renderLoop);
    timer.start(16);
}





MViewport::~MViewport(){
    delete sim;
}





void MViewport::resizeEvent(QResizeEvent *event){
    sim->setSize(
        event->size().width(), 
        event->size().height()
    );
}





/*
 * The Qt timer-driven render loop (60 FPS).
 * Briefly locks the physics engine to safely extract render data,
 * ensuring no geometry mutates mid-draw, then updates UI telemetry.
 */
void MViewport::renderLoop(){
    QImage currentFrame;
    SimulationManager* simManager = Application::getInstance()->getSimulationManager();

    if (simManager) {
        std::lock_guard<std::mutex> lock(simManager->physicsMutex);
        currentFrame = sim->render();
    } else {
        return; 
        currentFrame = sim->render();
    }

    setPixmap(QPixmap::fromImage(currentFrame));

    Application::getInstance()->getEditor()->inspector->updateLiveValues(); 
}