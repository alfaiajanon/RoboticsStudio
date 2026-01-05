#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include "Camera.h"
#include "Core/InputManager.h"
#include "Core/Log.h"

class CameraController{
    protected:
        Camera *cam;

    public:
        void setCamera(Camera* cam){
            this->cam = cam;
        }
        virtual void onMouseMove(int dx, int dy){};
        virtual void onMouseScroll(double delta){};
        virtual void onMouseDown(int x, int y){};
        virtual void onMouseUp(){};
        virtual void onKeyPress(int key, int mods){};
        virtual void onKeyRelease(int key, int mods){};
};




class OrbitCameraController : public CameraController{
    private:
        int lastX;
        int lastY;
        enum State{
            IDLE,
            ORBITING,
            PANNING
        };
        State state;

        void orbitHorizontal(double angle){
            Position position = cam->getPosition();
            Position target = cam->getTarget();

            double dx = position.x - target.x;
            double dy = position.y - target.y;

            double r = sqrt(dx*dx + dy*dy);
            double theta = atan2(dy, dx) + angle * M_PI / 180.0;

            position.x = target.x + r * cos(theta);
            position.y = target.y + r * sin(theta);

            cam->setPosition(position);
        }

        void orbitVertical(double angle){
            Position position = cam->getPosition();
            Position target = cam->getTarget();

            double dx = position.x - target.x;
            double dy = position.y - target.y;
            double dz = position.z - target.z;

            double r = sqrt(dx*dx + dy*dy + dz*dz);

            if (r < 0.0001) return; 
            double cosPhi = dz / r;
            if(cosPhi > 1.0) cosPhi = 1.0;
            if(cosPhi < -1.0) cosPhi = -1.0;


            double phi = acos(cosPhi) + angle * M_PI / 180.0;

            if(phi < 0.1) phi = 0.1;
            if(phi > M_PI - 0.1) phi = M_PI - 0.1;

            double newR = r * sin(phi);
            position.z = target.z + r * cos(phi);
            double theta = atan2(dy, dx);
            position.x = target.x + newR * cos(theta);
            position.y = target.y + newR * sin(theta);

            cam->setPosition(position);
        }

        void pan(double dx, double dy){
            Position pos  = cam->getPosition();
            Position tgt  = cam->getTarget();

            // 1. Compute forward vector (from camera to target)
            double fx = tgt.x - pos.x;
            double fy = tgt.y - pos.y;
            double fz = tgt.z - pos.z;

            double fl = sqrt(fx*fx + fy*fy + fz*fz);
            fx /= fl; fy /= fl; fz /= fl;    // normalize

            // 2. World up (MuJoCo, Godot, Unity, Blender use +Z as up)
            double upx = 0, upy = 0, upz = 1;

            // 3. Compute RIGHT = normalize(cross(forward, up))
            double rx = fy*upz - fz*upy;
            double ry = fz*upx - fx*upz;
            double rz = fx*upy - fy*upx;

            double rl = sqrt(rx*rx + ry*ry + rz*rz);
            if (rl < 1e-9) return; // handle forward ~ parallel to up
            rx /= rl; ry /= rl; rz /= rl;

            // 4. Compute camera UP = cross(right, forward)
            double ux = ry*fz - rz*fy;
            double uy = rz*fx - rx*fz;
            double uz = rx*fy - ry*fx;

            double ul = sqrt(ux*ux + uy*uy + uz*uz);
            ux /= ul; uy /= ul; uz /= ul;

            // 5. Screen-space movement: right and up
            double s = pos.distanceTo(tgt);     // your sensitivity scale

            double dxw = -dx*(1+s);    // drag-right → move-left
            double dyw =  dy*(1+s);    // drag-up → move-up

            double px = rx*dxw + ux*dyw;
            double py = ry*dxw + uy*dyw;
            double pz = rz*dxw + uz*dyw;

            // 6. Apply to position AND target
            pos.x += px; pos.y += py; pos.z += pz;
            tgt.x += px; tgt.y += py; tgt.z += pz;

            cam->setPosition(pos);
            cam->setTarget(tgt);   // or cam->lookAt(tgt);
        }


        void zoom(double delta){
            Position position = cam->getPosition();
            Position target = cam->getTarget();

            double dx = position.x - target.x;
            double dy = position.y - target.y;
            double dz = position.z - target.z;

            double dist = sqrt(dx*dx + dy*dy + dz*dz);
            dist += delta;
            if(dist < 0.1) dist = 0.1; 

            double scale = dist / sqrt(dx*dx + dy*dy + dz*dz);
            position.x = target.x + dx * scale;
            position.y = target.y + dy * scale;
            position.z = target.z + dz * scale;

            cam->setPosition(position);
        }
        


    public:
        float ORBIT_SENS = 0.5; 
        float ZOOM_SENS = 0.1;
        float PAN_SENS = 0.001;

        OrbitCameraController(){
            state = IDLE;
            lastX = 10;
            lastY = 10;
        }

        void onMouseMove(int x, int y) override {
            if(state == ORBITING){
                int dx = x - lastX;
                int dy = y - lastY;

                orbitHorizontal(-dx * ORBIT_SENS);
                orbitVertical(-dy * ORBIT_SENS);
                
                lastX = x;
                lastY = y;
            }else if(state == PANNING){
                int dx = x - lastX;
                int dy = y - lastY;

                pan(dx * 0.001, dy * 0.001);
                
                lastX = x;
                lastY = y;
            }
        }

        void onMouseScroll(double delta) override {
            zoom(-delta * ZOOM_SENS);
        }

        void onMouseDown(int button, int mods) override{
            double x, y;
            InputManager::getInstance().getCursorPosition(x, y);
            lastX = x;
            lastY = y;

            bool ctrl = (mods & Qt::ControlModifier) != 0;
            bool middleButton = (button == Qt::MiddleButton);

            if (ctrl || middleButton) {
                state = PANNING;
            } else {
                state = ORBITING;
            }
        }


        void onMouseUp() override {
            state = IDLE;
        }
};





#endif