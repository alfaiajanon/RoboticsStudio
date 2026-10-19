#pragma once

#include <QString>

class Project;

/*
 * Starter firmware sketch for a project: lists its pins (robot_pins.h) and
 * attaches the first servo with the part's own pulse range. With no project
 * (a new, empty one) it is a bare setup()/loop() sketch.
 */
QString firmwareTemplate(Project* project);
