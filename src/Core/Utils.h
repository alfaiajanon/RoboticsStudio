#pragma once

#include <bits/stdc++.h>
#include "mujoco/mujoco.h"
#include "mujoco/mjrender.h"

class Position{
    public:
        double x,y,z;
        Position():x(0),y(0),z(0){}
        Position(double x_, double y_, double z_):x(x_),y(y_),z(z_){}
        double distanceTo(const Position& other){
            double dx = x - other.x;
            double dy = y - other.y;
            double dz = z - other.z;
            return sqrt(dx*dx + dy*dy + dz*dz);
        }
};