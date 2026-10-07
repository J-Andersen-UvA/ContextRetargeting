#pragma once

#include "context_retargeting/context/geometry.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace context_retargeting::context {

using BoneIndex = std::uint32_t;
inline constexpr BoneIndex invalidBoneIndex =
    std::numeric_limits<BoneIndex>::max();

struct Bone
{
    BoneIndex parent = invalidBoneIndex;
};

struct TransformHierarchy
{
    std::vector<Bone> bones;
};

struct LocalPose
{
    std::vector<RigidTransform> transforms;
};

struct GlobalPose
{
    std::vector<RigidTransform> transforms;
};

struct PoseBatch
{
    std::vector<LocalPose> frames;
    Scalar secondsPerFrame = 0.0;
};

CONTEXTRETARGETINGCORE_API void validateHierarchy(const TransformHierarchy& hierarchy);
CONTEXTRETARGETINGCORE_API void validatePose(
    const TransformHierarchy& hierarchy,
    const LocalPose& pose);

CONTEXTRETARGETINGCORE_API void calculateGlobalPose(
    const TransformHierarchy& hierarchy,
    const LocalPose& localPose,
    GlobalPose& globalPose);

} // namespace context_retargeting::context
