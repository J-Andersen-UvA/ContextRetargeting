#include "test_support.hpp"

#include "context_retargeting/context/pose.hpp"

#include <numbers>
#include <stdexcept>

using namespace context_retargeting::context;

namespace {

void requireVectorNear(Vector3 actual, Vector3 expected, Scalar tolerance)
{
    CRT_REQUIRE_NEAR(actual.x, expected.x, tolerance);
    CRT_REQUIRE_NEAR(actual.y, expected.y, tolerance);
    CRT_REQUIRE_NEAR(actual.z, expected.z, tolerance);
}

} // namespace

CRT_TEST("quaternion multiplication applies the right rotation first")
{
    const Quaternion rotateX = Quaternion::fromAxisAngle(
        {1.0, 0.0, 0.0},
        std::numbers::pi / 2.0);
    const Quaternion rotateZ = Quaternion::fromAxisAngle(
        {0.0, 0.0, 1.0},
        std::numbers::pi / 2.0);
    const Vector3 value{0.0, 1.0, 0.0};

    const Vector3 combined = (rotateZ * rotateX).rotateVector(value);
    const Vector3 sequential = rotateZ.rotateVector(rotateX.rotateVector(value));

    requireVectorNear(combined, sequential, 1.0e-12);
}

CRT_TEST("transform multiplication applies the left transform first")
{
    const RigidTransform left{
        Quaternion::identity(),
        {1.0, 0.0, 0.0}};
    const RigidTransform right{
        Quaternion::fromAxisAngle(
            {0.0, 0.0, 1.0},
            std::numbers::pi / 2.0),
        {0.0, 2.0, 0.0}};
    const Vector3 point{2.0, 0.0, 0.0};

    const Vector3 combined = (left * right).transformPosition(point);
    const Vector3 sequential = right.transformPosition(left.transformPosition(point));

    requireVectorNear(combined, sequential, 1.0e-12);
}

CRT_TEST("forward kinematics uses Unreal transform composition order")
{
    const TransformHierarchy hierarchy{{
        {invalidBoneIndex},
        {0}}};
    LocalPose localPose;
    localPose.transforms = {
        {Quaternion::fromAxisAngle(
             {0.0, 0.0, 1.0},
             std::numbers::pi / 2.0),
         {10.0, 0.0, 0.0}},
        {Quaternion::identity(), {1.0, 0.0, 0.0}}};
    GlobalPose globalPose;

    calculateGlobalPose(hierarchy, localPose, globalPose);

    requireVectorNear(globalPose.transforms[1].translation, {10.0, 1.0, 0.0}, 1.0e-12);
}

CRT_TEST("hierarchy validation rejects children before their parent")
{
    const TransformHierarchy hierarchy{{
        {invalidBoneIndex},
        {2},
        {0}}};

    CRT_REQUIRE_THROWS(std::invalid_argument, validateHierarchy(hierarchy));
}
