#pragma once

#include <cmath>
#include "mujoco/mujoco.h"
#include "mujoco/mjrender.h"

class Position {
public:
    double x, y, z;

    Position();
    Position(double x_, double y_, double z_);

    double distanceTo(const Position& other) const;
    Position operator+(const Position& other) const;
    Position operator-(const Position& other) const;
    Position operator-() const;
};




class Rotation {
public:
    double w, x, y, z;

    Rotation();
    Rotation(double w_, double x_, double y_, double z_);

    Rotation operator*(const Rotation& other) const;
    Rotation inverse() const;
    Position rotate(const Position& pos) const;
};




class Transform {
public:
    Position position;
    Rotation rotation;

    Transform();
    Transform(const Position& p, const Rotation& r);

    Transform operator*(const Transform& other) const;
    Transform inverse() const;
};