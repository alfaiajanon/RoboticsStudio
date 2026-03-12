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