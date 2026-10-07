#pragma once

#include <cmath>

namespace context_retargeting::context {

using Scalar = double;

struct CONTEXTRETARGETINGCORE_API Vector3
{
    Scalar x = 0.0;
    Scalar y = 0.0;
    Scalar z = 0.0;

    [[nodiscard]] Scalar lengthSquared() const noexcept;
    [[nodiscard]] Scalar length() const noexcept;
    [[nodiscard]] Vector3 normalized() const;
    [[nodiscard]] bool isFinite() const noexcept;

    [[nodiscard]] static Scalar dot(Vector3 left, Vector3 right) noexcept;
    [[nodiscard]] static Vector3 cross(Vector3 left, Vector3 right) noexcept;
};

[[nodiscard]] CONTEXTRETARGETINGCORE_API Vector3 operator+(Vector3 left, Vector3 right) noexcept;
[[nodiscard]] CONTEXTRETARGETINGCORE_API Vector3 operator-(Vector3 left, Vector3 right) noexcept;
[[nodiscard]] CONTEXTRETARGETINGCORE_API Vector3 operator-(Vector3 value) noexcept;
[[nodiscard]] CONTEXTRETARGETINGCORE_API Vector3 operator*(Vector3 value, Scalar scale) noexcept;
[[nodiscard]] CONTEXTRETARGETINGCORE_API Vector3 operator*(Scalar scale, Vector3 value) noexcept;
[[nodiscard]] CONTEXTRETARGETINGCORE_API Vector3 operator/(Vector3 value, Scalar scale);
[[nodiscard]] CONTEXTRETARGETINGCORE_API bool operator==(Vector3 left, Vector3 right) noexcept;

struct CONTEXTRETARGETINGCORE_API Quaternion
{
    Scalar x = 0.0;
    Scalar y = 0.0;
    Scalar z = 0.0;
    Scalar w = 1.0;

    [[nodiscard]] static Quaternion identity() noexcept;
    [[nodiscard]] static Quaternion fromAxisAngle(Vector3 axis, Scalar angleRadians);

    [[nodiscard]] Scalar lengthSquared() const noexcept;
    [[nodiscard]] Quaternion normalized() const;
    [[nodiscard]] Quaternion inverse() const;
    [[nodiscard]] Vector3 rotateVector(Vector3 value) const noexcept;
    [[nodiscard]] bool isFinite() const noexcept;
};

// Matches FQuat: left * right applies right first and then left.
[[nodiscard]] CONTEXTRETARGETINGCORE_API Quaternion operator*(Quaternion left, Quaternion right) noexcept;
[[nodiscard]] CONTEXTRETARGETINGCORE_API bool operator==(Quaternion left, Quaternion right) noexcept;

struct CONTEXTRETARGETINGCORE_API RigidTransform
{
    Quaternion rotation;
    Vector3 translation;

    [[nodiscard]] static RigidTransform identity() noexcept;
    [[nodiscard]] Vector3 transformPosition(Vector3 position) const noexcept;
    [[nodiscard]] RigidTransform inverse() const;
    [[nodiscard]] bool isFinite() const noexcept;
};

// Matches FTransform: left * right applies left first and then right.
[[nodiscard]] CONTEXTRETARGETINGCORE_API RigidTransform operator*(
    const RigidTransform& left,
    const RigidTransform& right);

} // namespace context_retargeting::context
