#pragma once

#include <QString>

class Project;

/*
 * Starter firmware sketch for a project: lists its pins and attaches the
 * first servo with the part's own pulse range.
 *   Prototype: the names in the generated robot_pins.h.
 *   Real: the board's own pin numbers, as `const int` lines of the sketch
 *         (the user's from then on; nothing is generated at the Real level).
 * With no project (a new, empty one) it is a bare setup()/loop() sketch for
 * that level.
 */
QString firmwareTemplate(Project* project, bool realLevel = false);
