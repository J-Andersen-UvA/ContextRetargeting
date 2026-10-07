#include "context_retargeting/context/pose.hpp"

#include <cmath>
#include <stdexcept>

namespace context_retargeting::context {

void validateHierarchy(const TransformHierarchy& hierarchy)
{
    if (hierarchy.bones.empty()) {
        throw std::invalid_argument("Transform hierarchy must contain a root bone");
    }
    if (hierarchy.bones.front().parent != invalidBoneIndex) {
        throw std::invalid_argument("The first hierarchy bone must be the root");
    }

    for (std::size_t index = 1; index < hierarchy.bones.size(); ++index) {
        const BoneIndex parent = hierarchy.bones[index].parent;
        if (parent == invalidBoneIndex || parent >= index) {
            throw std::invalid_argument(
                "Hierarchy bones must be in parent-before-child order");
        }
    }
}

void validatePose(
    const TransformHierarchy& hierarchy,
    const LocalPose& pose)
{
    if (pose.transforms.size() != hierarchy.bones.size()) {
        throw std::invalid_argument(
            "Pose transform count must match hierarchy bone count");
    }

    for (const RigidTransform& transform : pose.transforms) {
        const Scalar rotationLengthSquared = transform.rotation.lengthSquared();
        if (!transform.isFinite() ||
            std::abs(rotationLengthSquared - 1.0) > 1.0e-6) {
            throw std::invalid_argument(
                "Pose rotations must be finite and normalized");
        }
    }
}

void calculateGlobalPose(
    const TransformHierarchy& hierarchy,
    const LocalPose& localPose,
    GlobalPose& globalPose)
{
    validateHierarchy(hierarchy);
    validatePose(hierarchy, localPose);

    globalPose.transforms.resize(localPose.transforms.size());
    globalPose.transforms[0] = localPose.transforms[0];

    for (std::size_t index = 1; index < localPose.transforms.size(); ++index) {
        const BoneIndex parent = hierarchy.bones[index].parent;
        globalPose.transforms[index] =
            localPose.transforms[index] * globalPose.transforms[parent];
    }
}

} // namespace context_retargeting::context
