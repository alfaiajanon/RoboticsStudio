#ifndef CAMERA_H
#define CAMERA_H

#include "Utils/Spatial.h"

class Camera{
    mjvCamera *cam;
    Position position;
    Position target;

    void update(){
        cam->lookat[0] = target.x;
        cam->lookat[1] = target.y;
        cam->lookat[2] = target.z;

        double tx = target.x;
        double ty = target.y;
        double tz = target.z;
        double dx = tx - position.x;
        double dy = ty - position.y;
        double dz = tz - position.z;

        double dist = sqrt(dx*dx + dy*dy + dz*dz);

        if (dist <= 0.0001) {
            dist = 0.0001; 
        }
        
        cam->distance = dist;
        cam->azimuth   = atan2(dy, dx) * 180.0 / M_PI;
        cam->elevation = asin(dz / dist) * 180.0 / M_PI;
    }


    public:
        Camera(){
            cam = new mjvCamera();
            position = Position(0, 0, 0);
            target = Position(0, 0, 0);
        }

        Camera(mjvCamera* existingCam){
            cam = existingCam;
            position = Position(0, 0, 0);
            target = Position(0, 0, 0);
        }

        void lookAt(Position t){
            target = t;
            update();
        }

        void setPosition(Position p){
            position = p;
            update();
        }

        void setTarget(Position t){
            target = t;
            update();
        }

        Position getPosition(){
            return position;
        }

        Position getTarget(){
            return target;
        }

        mjvCamera* getMJCam(){
            return cam;
        }
};


#endif