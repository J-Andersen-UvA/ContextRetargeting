#include "context_retargeting/context/geometry.hpp"

#include <stdexcept>

namespace context_retargeting::context {

Scalar Vector3::lengthSquared() const noexcept
{
    return x * x + y * y + z * z;
}

Scalar Vector3::length() const noexcept
{
    return std::sqrt(lengthSquared());
}

Vector3 Vector3::normalized() const
{
    const Scalar magnitude = length();
    if (!std::isfinite(magnitude) || magnitude <= 0.0) {
        throw std::invalid_argument("Cannot normalize a zero or non-finite vector");
    }
    return *this / magnitude;
}

bool Vector3::isFinite() const noexcept
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

Scalar Vector3::dot(Vector3 left, Vector3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vector3 Vector3::cross(Vector3 left, Vector3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

Vector3 operator+(Vector3 left, Vector3 right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vector3 operator-(Vector3 left, Vector3 right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vector3 operator-(Vector3 value) noexcept
{
    return {-value.x, -value.y, -value.z};
}

Vector3 operator*(Vector3 value, Scalar scale) noexcept
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vector3 operator*(Scalar scale, Vector3 value) noexcept
{
    return value * scale;
}

Vector3 operator/(Vector3 value, Scalar scale)
{
    if (!std::isfinite(scale) || scale == 0.0) {
        throw std::invalid_argument("Vector divisor must be finite and non-zero");
    }
    return {value.x / scale, value.y / scale, value.z / scale};
}

bool operator==(Vector3 left, Vector3 right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

Quaternion Quaternion::identity() noexcept
{
    return {};
}

Quaternion Quaternion::fromAxisAngle(Vector3 axis, Scalar angleRadians)
{
    if (!std::isfinite(angleRadians)) {
        throw std::invalid_argument("Rotation angle must be finite");
    }

    const Vector3 unitAxis = axis.normalized();
    const Scalar halfAngle = angleRadians * 0.5;
    const Scalar sine = std::sin(halfAngle);
    return {
        unitAxis.x * sine,
        unitAxis.y * sine,
        unitAxis.z * sine,
        std::cos(halfAngle)};
}

Scalar Quaternion::lengthSquared() const noexcept
{
    return x * x + y * y + z * z + w * w;
}

Quaternion Quaternion::normalized() const
{
    const Scalar magnitudeSquared = lengthSquared();
    if (!std::isfinite(magnitudeSquared) || magnitudeSquared <= 0.0) {
        throw std::invalid_argument("Cannot normalize a zero or non-finite quaternion");
    }

    const Scalar inverseMagnitude = 1.0 / std::sqrt(magnitudeSquared);
    return {
        x * inverseMagnitude,
        y * inverseMagnitude,
        z * inverseMagnitude,
        w * inverseMagnitude};
}

Quaternion Quaternion::inverse() const
{
    const Scalar magnitudeSquared = lengthSquared();
    if (!std::isfinite(magnitudeSquared) || magnitudeSquared <= 0.0) {
        throw std::invalid_argument("Cannot invert a zero or non-finite quaternion");
    }
    return {
        -x / magnitudeSquared,
        -y / magnitudeSquared,
        -z / magnitudeSquared,
        w / magnitudeSquared};
}

Vector3 Quaternion::rotateVector(Vector3 value) const noexcept
{
    const Vector3 quaternionVector{x, y, z};
    const Vector3 twiceCross = 2.0 * Vector3::cross(quaternionVector, value);
    return value + w * twiceCross + Vector3::cross(quaternionVector, twiceCross);
}

bool Quaternion::isFinite() const noexcept
{
    return std::isfinite(x) && std::isfinite(y) &&
        std::isfinite(z) && std::isfinite(w);
}

Quaternion operator*(Quaternion left, Quaternion right) noexcept
{
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
}

bool operator==(Quaternion left, Quaternion right) noexcept
{
    return left.x == right.x && left.y == right.y &&
        left.z == right.z && left.w == right.w;
}

RigidTransform RigidTransform::identity() noexcept
{
    return {};
}

Vector3 RigidTransform::transformPosition(Vector3 position) const noexcept
{
    return rotation.rotateVector(position) + translation;
}

RigidTransform RigidTransform::inverse() const
{
    const Quaternion inverseRotation = rotation.inverse().normalized();
    return {
        inverseRotation,
        inverseRotation.rotateVector(-translation)};
}

bool RigidTransform::isFinite() const noexcept
{
    return rotation.isFinite() && translation.isFinite();
}

RigidTransform operator*(
    const RigidTransform& left,
    const RigidTransform& right)
{
    return {
        (right.rotation * left.rotation).normalized(),
        right.rotation.rotateVector(left.translation) + right.translation};
}

} // namespace context_retargeting::context
