#include "Spatial.h"

Position::Position() : x(0), y(0), z(0) {}




Position::Position(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}




/*
 * Calculates the Euclidean distance between two points in 3D space.
 */
double Position::distanceTo(const Position& other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    double dz = z - other.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}




Position Position::operator+(const Position& other) const {
    return Position(x + other.x, y + other.y, z + other.z);
}




Position Position::operator-(const Position& other) const {
    return Position(x - other.x, y - other.y, z - other.z);
}




Position Position::operator-() const {
    return Position(-x, -y, -z);
}




Rotation::Rotation() : w(1), x(0), y(0), z(0) {}




Rotation::Rotation(double w_, double x_, double y_, double z_) : w(w_), x(x_), y(y_), z(z_) {}


Rotation::Rotation(double roll, double pitch, double yaw) {
    double halfRoll = roll * 0.5 * (M_PI / 180.0);
    double halfPitch = pitch * 0.5 * (M_PI / 180.0);
    double halfYaw = yaw * 0.5 * (M_PI / 180.0);

    double cr = std::cos(halfRoll);
    double sr = std::sin(halfRoll);
    double cp = std::cos(halfPitch);
    double sp = std::sin(halfPitch);
    double cy = std::cos(halfYaw);
    double sy = std::sin(halfYaw);

    w = cr * cp * cy + sr * sp * sy;
    x = sr * cp * cy - cr * sp * sy;
    y = cr * sp * cy + sr * cp * sy;
    z = cr * cp * sy - sr * sp * cy;
}


double Rotation::roll() const {
    return std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)) * (180.0 / M_PI);
}

double Rotation::pitch() const {
    double sinp = 2.0 * (w * y - z * x);
    if (std::abs(sinp) >= 1)
        return std::copysign(90.0, sinp); // use 90 degrees if out of range
    else
        return std::asin(sinp) * (180.0 / M_PI);
}

double Rotation::yaw() const {
    return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)) * (180.0 / M_PI);
}



/*
 * Multiplies two quaternions to combine their rotations.
 * Applies the current rotation followed by the other rotation.
 */
Rotation Rotation::operator*(const Rotation& other) const {
    return Rotation(
        w * other.w - x * other.x - y * other.y - z * other.z,
        w * other.x + x * other.w + y * other.z - z * other.y,
        w * other.y - x * other.z + y * other.w + z * other.x,
        w * other.z + x * other.y - y * other.x + z * other.w
    );
}




/*
 * Returns the conjugate of the unit quaternion.
 * This effectively reverses the direction of the rotation.
 */
Rotation Rotation::inverse() const {
    return Rotation(w, -x, -y, -z);
}




/*
 * Rotates a 3D position vector using the quaternion.
 * Converts the vector to a pure quaternion for the standard q * p * q^-1 operation.
 */
Position Rotation::rotate(const Position& pos) const {
    Rotation p(0, pos.x, pos.y, pos.z);
    Rotation rotated = (*this) * p * this->inverse();
    return Position(rotated.x, rotated.y, rotated.z);
}




Transform::Transform() : position(), rotation() {}




Transform::Transform(const Position& p, const Rotation& r) : position(p), rotation(r) {}




/*
 * Combines two spatial transforms into a single transform.
 * Translates and rotates the child transform into the parent's coordinate frame.
 */
Transform Transform::operator*(const Transform& other) const {
    Position newPos = this->position + this->rotation.rotate(other.position);
    Rotation newRot = this->rotation * other.rotation;
    return Transform(newPos, newRot);
}




/*
 * Calculates the inverse of the spatial transform.
 * Essential for kinematic tree inversion and calculating relative child offsets.
 */
Transform Transform::inverse() const {
    Rotation invRot = this->rotation.inverse();
    Position invPos = invRot.rotate(-this->position);
    return Transform(invPos, invRot);
}