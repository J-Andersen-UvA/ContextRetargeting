#pragma once

#include "context_retargeting/context/pose.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace context_retargeting::context {

using ContextPointIndex = std::uint32_t;
inline constexpr ContextPointIndex invalidContextPointIndex =
    std::numeric_limits<ContextPointIndex>::max();

struct ContextPointDefinition
{
    BoneIndex parentBone = invalidBoneIndex;
    Vector3 localPosition;
    Vector3 localNormal{1.0, 0.0, 0.0};
};

struct CharacterDefinition
{
    TransformHierarchy hierarchy;
    std::vector<ContextPointDefinition> contextPoints;
    Scalar height = 1.0;
};

struct DistanceRelationship
{
    ContextPointIndex firstPoint = invalidContextPointIndex;
    ContextPointIndex secondPoint = invalidContextPointIndex;
    Scalar weight = 1.0;
    bool useDistance = true;
    bool useDirection = true;
    bool usePenetration = true;
    bool useAdaptiveWeight = true;
};

struct ContextEnvironment
{
    Vector3 upAxis{0.0, 0.0, 1.0};
    Scalar sourceGroundHeight = 0.0;
    Scalar targetGroundHeight = 0.0;
};

struct AdaptiveWeightSettings
{
    bool enabled = true;
    bool useTargetWeights = true;
    Scalar interactionMinimumDistanceRatio = 0.05;
    Scalar interactionMaximumDistanceRatio = 0.15;
    Scalar floorMinimumHeightRatio = 0.05;
    Scalar floorMaximumHeightRatio = 0.15;
};

struct BoneRotationDegreeOfFreedom
{
    BoneIndex bone = invalidBoneIndex;
    Vector3 localAxis;
};

struct ContextDefinition
{
    CharacterDefinition sourceCharacter;
    CharacterDefinition targetCharacter;
    std::vector<DistanceRelationship> distanceRelationships;
    std::vector<BoneRotationDegreeOfFreedom> boneRotationDegreesOfFreedom;
    ContextEnvironment environment;
    AdaptiveWeightSettings adaptiveWeights;
};

struct ContextBatchInput
{
    PoseBatch sourcePoses;
    PoseBatch initialTargetPoses;
};

struct ContextLossWeights
{
    Scalar distance = 1.0;
    Scalar direction = 1.0;
    Scalar penetration = 1.0;
    Scalar height = 1.0;
    Scalar pointPositionRegularization = 1.0;
    Scalar pointJerk = 0.0;
};

struct CONTEXTRETARGETINGCORE_API ContextLossComponents
{
    Scalar distance = 0.0;
    Scalar direction = 0.0;
    Scalar penetration = 0.0;
    Scalar height = 0.0;
    Scalar pointPositionRegularization = 0.0;
    Scalar pointJerk = 0.0;

    [[nodiscard]] Scalar weightedTotal(
        const ContextLossWeights& weights) const noexcept;
};

struct CONTEXTRETARGETINGCORE_API ContextGradientMagnitudes
{
    Scalar distance = 0.0;
    Scalar direction = 0.0;
    Scalar penetration = 0.0;
    Scalar height = 0.0;
    Scalar pointPositionRegularization = 0.0;
    Scalar pointJerk = 0.0;
    Scalar total = 0.0;
};

CONTEXTRETARGETINGCORE_API void validateContextDefinition(
    const ContextDefinition& definition);
CONTEXTRETARGETINGCORE_API void validateContextBatchInput(
    const ContextDefinition& definition,
    const ContextBatchInput& input);
CONTEXTRETARGETINGCORE_API void validateContextLossWeights(
    const ContextLossWeights& weights);

} // namespace context_retargeting::context
