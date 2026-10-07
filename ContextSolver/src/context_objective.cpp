#include "context_retargeting/context/context_objective.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace context_retargeting::context {
namespace {

Scalar squaredDistance(Vector3 first, Vector3 second) noexcept
{
    return (first - second).lengthSquared();
}

Scalar clampedProximityWeight(
    Scalar value,
    Scalar minimum,
    Scalar maximum) noexcept
{
    return std::clamp(1.0 - (value - minimum) / (maximum - minimum), 0.0, 1.0);
}

Scalar directionError(Vector3 sourceOffset, Vector3 targetOffset) noexcept
{
    const Scalar sourceLengthSquared = sourceOffset.lengthSquared();
    const Scalar targetLengthSquared = targetOffset.lengthSquared();
    if (sourceLengthSquared <= 0.0 || targetLengthSquared <= 0.0) {
        return sourceLengthSquared == targetLengthSquared ? 0.0 : 1.0;
    }

    const Scalar cosine = Vector3::dot(sourceOffset, targetOffset) /
        std::sqrt(sourceLengthSquared * targetLengthSquared);
    const Scalar cosineDistance =
        1.0 - std::clamp(cosine, -1.0, 1.0);
    return cosineDistance * cosineDistance;
}

void validateCharacter(const CharacterDefinition& character)
{
    validateHierarchy(character.hierarchy);
    for (const ContextPointDefinition& point : character.contextPoints) {
        if (point.parentBone >= character.hierarchy.bones.size()) {
            throw std::invalid_argument("Context point references an invalid bone");
        }
        if (!point.localPosition.isFinite()) {
            throw std::invalid_argument("Context point position must be finite");
        }
        if (!point.localNormal.isFinite() || point.localNormal.lengthSquared() <= 0.0) {
            throw std::invalid_argument("Context point normal must be finite and non-zero");
        }
    }
    if (!std::isfinite(character.height) || character.height <= 0.0) {
        throw std::invalid_argument("Character height must be finite and positive");
    }
}

void validatePoseBatch(
    const TransformHierarchy& hierarchy,
    const PoseBatch& poses)
{
    if (poses.frames.empty()) {
        throw std::invalid_argument("Pose batch must contain at least one frame");
    }
    if (!std::isfinite(poses.secondsPerFrame) || poses.secondsPerFrame <= 0.0) {
        throw std::invalid_argument("Pose batch secondsPerFrame must be finite and positive");
    }
    for (const LocalPose& pose : poses.frames) {
        validatePose(hierarchy, pose);
    }
}

} // namespace

Scalar ContextLossComponents::weightedTotal(
    const ContextLossWeights& weights) const noexcept
{
    return weights.distance * distance +
        weights.direction * direction +
        weights.penetration * penetration +
        weights.height * height +
        weights.pointPositionRegularization * pointPositionRegularization +
        weights.pointJerk * pointJerk;
}

void validateContextDefinition(const ContextDefinition& definition)
{
    validateCharacter(definition.sourceCharacter);
    validateCharacter(definition.targetCharacter);

    if (definition.sourceCharacter.contextPoints.size() !=
        definition.targetCharacter.contextPoints.size()) {
        throw std::invalid_argument(
            "Source and target must have corresponding context points");
    }

    const std::size_t pointCount = definition.sourceCharacter.contextPoints.size();
    for (const DistanceRelationship& relationship :
         definition.distanceRelationships) {
        if (relationship.firstPoint >= pointCount ||
            relationship.secondPoint >= pointCount) {
            throw std::invalid_argument(
                "Distance relationship references an invalid context point");
        }
        if (!std::isfinite(relationship.weight) || relationship.weight < 0.0) {
            throw std::invalid_argument(
                "Distance relationship weight must be finite and non-negative");
        }
    }

    for (const BoneRotationDegreeOfFreedom& degreeOfFreedom :
         definition.boneRotationDegreesOfFreedom) {
        if (degreeOfFreedom.bone >=
            definition.targetCharacter.hierarchy.bones.size()) {
            throw std::invalid_argument(
                "Bone rotation degree of freedom references an invalid target bone");
        }
        if (!degreeOfFreedom.localAxis.isFinite() ||
            degreeOfFreedom.localAxis.lengthSquared() <= 0.0) {
            throw std::invalid_argument(
                "Bone rotation degree of freedom axis must be finite and non-zero");
        }
    }

    if (!definition.environment.upAxis.isFinite() ||
        definition.environment.upAxis.lengthSquared() <= 0.0 ||
        !std::isfinite(definition.environment.sourceGroundHeight) ||
        !std::isfinite(definition.environment.targetGroundHeight)) {
        throw std::invalid_argument("Context environment is invalid");
    }

    const AdaptiveWeightSettings& adaptive = definition.adaptiveWeights;
    if (!std::isfinite(adaptive.interactionMinimumDistanceRatio) ||
        !std::isfinite(adaptive.interactionMaximumDistanceRatio) ||
        !std::isfinite(adaptive.floorMinimumHeightRatio) ||
        !std::isfinite(adaptive.floorMaximumHeightRatio) ||
        adaptive.interactionMinimumDistanceRatio < 0.0 ||
        adaptive.interactionMaximumDistanceRatio <=
            adaptive.interactionMinimumDistanceRatio ||
        adaptive.floorMinimumHeightRatio < 0.0 ||
        adaptive.floorMaximumHeightRatio <= adaptive.floorMinimumHeightRatio) {
        throw std::invalid_argument("Adaptive weight thresholds are invalid");
    }
}

void validateContextBatchInput(
    const ContextDefinition& definition,
    const ContextBatchInput& input)
{
    validatePoseBatch(
        definition.sourceCharacter.hierarchy,
        input.sourcePoses);
    validatePoseBatch(
        definition.targetCharacter.hierarchy,
        input.initialTargetPoses);

    if (input.sourcePoses.frames.size() !=
        input.initialTargetPoses.frames.size()) {
        throw std::invalid_argument(
            "Source and target pose batches must have the same frame count");
    }
    if (input.sourcePoses.secondsPerFrame !=
        input.initialTargetPoses.secondsPerFrame) {
        throw std::invalid_argument(
            "Source and target pose batches must use the same frame interval");
    }
}

void validateContextLossWeights(const ContextLossWeights& weights)
{
    if (!std::isfinite(weights.distance) || weights.distance < 0.0 ||
        !std::isfinite(weights.direction) || weights.direction < 0.0 ||
        !std::isfinite(weights.penetration) || weights.penetration < 0.0 ||
        !std::isfinite(weights.height) || weights.height < 0.0 ||
        !std::isfinite(weights.pointPositionRegularization) ||
        weights.pointPositionRegularization < 0.0 ||
        !std::isfinite(weights.pointJerk) || weights.pointJerk < 0.0) {
        throw std::invalid_argument(
            "Context loss weights must be finite and non-negative");
    }
}

ContextObjective::ContextObjective(
    const ContextDefinition& definition,
    const ContextBatchInput& input,
    ContextLossWeights lossWeights)
    : definition_(definition),
      input_(input),
      lossWeights_(lossWeights)
{
    validateContextDefinition(definition_);
    validateContextBatchInput(definition_, input_);
    validateContextLossWeights(lossWeights_);

    evaluatePointGeometry(
        definition_.sourceCharacter,
        input_.sourcePoses,
        sourceGlobalPoses_,
        sourcePointPositions_,
        sourcePointNormals_);
    evaluatePointGeometry(
        definition_.targetCharacter,
        input_.initialTargetPoses,
        initialTargetGlobalPoses_,
        initialTargetPointPositions_,
        initialTargetPointNormals_);

    candidateTargetPoses_ = input_.initialTargetPoses;
    candidateTargetGlobalPoses_.resize(input_.initialTargetPoses.frames.size());
    candidateTargetPointPositions_.resize(input_.initialTargetPoses.frames.size());
    candidateTargetPointNormals_.resize(input_.initialTargetPoses.frames.size());
}

std::size_t ContextObjective::parameterCount() const noexcept
{
    return input_.initialTargetPoses.frames.size() *
        definition_.boneRotationDegreesOfFreedom.size();
}

ContextLossComponents ContextObjective::evaluateLossComponents(
    optimization::ConstParameterView parameters)
{
    applyParameters(parameters, candidateTargetPoses_);
    evaluatePointGeometry(
        definition_.targetCharacter,
        candidateTargetPoses_,
        candidateTargetGlobalPoses_,
        candidateTargetPointPositions_,
        candidateTargetPointNormals_);

    ContextLossComponents components;

    for (std::size_t frame = 0;
         frame < candidateTargetPointPositions_.size();
         ++frame) {
        const ContextLossComponents frameComponents =
            evaluateSpatialLossComponents(
                frame,
                candidateTargetPointPositions_[frame],
                candidateTargetPointNormals_[frame]);
        components.distance += frameComponents.distance;
        components.direction += frameComponents.direction;
        components.penetration += frameComponents.penetration;
        components.height += frameComponents.height;
        components.pointPositionRegularization +=
            frameComponents.pointPositionRegularization;
    }

    for (std::size_t firstFrame = 0;
         firstFrame + 3 < candidateTargetPointPositions_.size();
         ++firstFrame) {
        components.pointJerk += evaluateJerkLoss(
            firstFrame,
            0,
            nullptr);
    }

    return components;
}

Scalar ContextObjective::evaluateLoss(
    optimization::ConstParameterView parameters)
{
    return evaluateLossComponents(parameters).weightedTotal(lossWeights_);
}

void ContextObjective::setTargetAdaptiveWeightMix(
    Scalar targetAdaptiveWeightMix)
{
    if (!std::isfinite(targetAdaptiveWeightMix) ||
        targetAdaptiveWeightMix < 0.0 || targetAdaptiveWeightMix > 1.0) {
        throw std::invalid_argument(
            "Target adaptive weight mix must be between zero and one");
    }
    targetAdaptiveWeightMix_ = targetAdaptiveWeightMix;
}

ContextGradientMagnitudes ContextObjective::evaluateCentralDifferenceGradient(
    optimization::ConstParameterView parameters,
    optimization::ParameterView gradientOut,
    const optimization::CentralDifferenceSettings& settings,
    ContextGradientVectors* gradientVectorsOut)
{
    if (parameters.size() != parameterCount() ||
        gradientOut.size() != parameters.size()) {
        throw std::invalid_argument(
            "Context gradient sizes must match the parameter count");
    }
    if (!std::isfinite(settings.absoluteStep) ||
        !std::isfinite(settings.relativeStep) ||
        settings.absoluteStep < 0.0 ||
        settings.relativeStep < 0.0 ||
        (settings.absoluteStep == 0.0 && settings.relativeStep == 0.0)) {
        throw std::invalid_argument("Central-difference settings are invalid");
    }

    applyParameters(parameters, candidateTargetPoses_);
    evaluatePointGeometry(
        definition_.targetCharacter,
        candidateTargetPoses_,
        candidateTargetGlobalPoses_,
        candidateTargetPointPositions_,
        candidateTargetPointNormals_);

    std::vector<std::vector<Scalar>> frozenTargetAdaptiveWeights(
        candidateTargetPointPositions_.size());
    for (std::size_t frame = 0;
         frame < candidateTargetPointPositions_.size();
         ++frame) {
        frozenTargetAdaptiveWeights[frame] = calculateTargetAdaptiveWeights(
            candidateTargetPointPositions_[frame]);
    }

    const std::size_t degreesOfFreedomPerFrame =
        definition_.boneRotationDegreesOfFreedom.size();
    if (gradientVectorsOut != nullptr) {
        gradientVectorsOut->distance.assign(parameters.size(), 0.0);
        gradientVectorsOut->direction.assign(parameters.size(), 0.0);
        gradientVectorsOut->penetration.assign(parameters.size(), 0.0);
        gradientVectorsOut->height.assign(parameters.size(), 0.0);
        gradientVectorsOut->pointPositionRegularization.assign(
            parameters.size(), 0.0);
        gradientVectorsOut->pointJerk.assign(parameters.size(), 0.0);
        gradientVectorsOut->total.assign(parameters.size(), 0.0);
    }
    ContextGradientMagnitudes squaredMagnitudes;
    PointPositions perturbedPositions;
    PointNormals perturbedNormals;
    for (std::size_t parameter = 0; parameter < parameters.size(); ++parameter) {
        const Scalar original = parameters[parameter];
        if (!std::isfinite(original)) {
            throw std::invalid_argument(
                "Central differences require finite parameters");
        }
        const Scalar step =
            settings.absoluteStep + settings.relativeStep * std::abs(original);
        if (!std::isfinite(step) || step <= 0.0 || original + step == original) {
            throw std::invalid_argument(
                "Central-difference step is not representable for a parameter");
        }

        const std::size_t frame = parameter / degreesOfFreedomPerFrame;
        const std::size_t degree = parameter % degreesOfFreedomPerFrame;
        evaluatePerturbedFrameGeometry(
            parameters,
            frame,
            degree,
            step,
            perturbedPositions,
            perturbedNormals);
        const ContextLossComponents lossPlus = evaluateAffectedLossComponents(
            frame,
            perturbedPositions,
            perturbedNormals,
            frozenTargetAdaptiveWeights[frame]);

        evaluatePerturbedFrameGeometry(
            parameters,
            frame,
            degree,
            -step,
            perturbedPositions,
            perturbedNormals);
        const ContextLossComponents lossMinus = evaluateAffectedLossComponents(
            frame,
            perturbedPositions,
            perturbedNormals,
            frozenTargetAdaptiveWeights[frame]);
        const Scalar inverseSpan = 1.0 / (2.0 * step);
        const Scalar distanceGradient = lossWeights_.distance *
            (lossPlus.distance - lossMinus.distance) * inverseSpan;
        const Scalar directionGradient = lossWeights_.direction *
            (lossPlus.direction - lossMinus.direction) * inverseSpan;
        const Scalar penetrationGradient = lossWeights_.penetration *
            (lossPlus.penetration - lossMinus.penetration) * inverseSpan;
        const Scalar heightGradient = lossWeights_.height *
            (lossPlus.height - lossMinus.height) * inverseSpan;
        const Scalar regularizationGradient =
            lossWeights_.pointPositionRegularization *
            (lossPlus.pointPositionRegularization -
             lossMinus.pointPositionRegularization) * inverseSpan;
        const Scalar jerkGradient = lossWeights_.pointJerk *
            (lossPlus.pointJerk - lossMinus.pointJerk) * inverseSpan;
        const Scalar totalGradient =
            distanceGradient + directionGradient + penetrationGradient +
            heightGradient + regularizationGradient + jerkGradient;
        gradientOut[parameter] = totalGradient;
        if (gradientVectorsOut != nullptr) {
            gradientVectorsOut->distance[parameter] = distanceGradient;
            gradientVectorsOut->direction[parameter] = directionGradient;
            gradientVectorsOut->penetration[parameter] = penetrationGradient;
            gradientVectorsOut->height[parameter] = heightGradient;
            gradientVectorsOut->pointPositionRegularization[parameter] =
                regularizationGradient;
            gradientVectorsOut->pointJerk[parameter] = jerkGradient;
            gradientVectorsOut->total[parameter] = totalGradient;
        }
        squaredMagnitudes.distance += distanceGradient * distanceGradient;
        squaredMagnitudes.direction += directionGradient * directionGradient;
        squaredMagnitudes.penetration +=
            penetrationGradient * penetrationGradient;
        squaredMagnitudes.height += heightGradient * heightGradient;
        squaredMagnitudes.pointPositionRegularization +=
            regularizationGradient * regularizationGradient;
        squaredMagnitudes.pointJerk += jerkGradient * jerkGradient;
        squaredMagnitudes.total += totalGradient * totalGradient;
    }

    squaredMagnitudes.distance = std::sqrt(squaredMagnitudes.distance);
    squaredMagnitudes.direction = std::sqrt(squaredMagnitudes.direction);
    squaredMagnitudes.penetration = std::sqrt(squaredMagnitudes.penetration);
    squaredMagnitudes.height = std::sqrt(squaredMagnitudes.height);
    squaredMagnitudes.pointPositionRegularization =
        std::sqrt(squaredMagnitudes.pointPositionRegularization);
    squaredMagnitudes.pointJerk = std::sqrt(squaredMagnitudes.pointJerk);
    squaredMagnitudes.total = std::sqrt(squaredMagnitudes.total);
    return squaredMagnitudes;
}

ContextLossComponents ContextObjective::evaluateSpatialLossComponents(
    std::size_t frame,
    const PointPositions& candidatePoints,
    const PointNormals& candidateNormals,
    const std::vector<Scalar>* frozenTargetAdaptiveWeights) const
{
    const PointPositions& sourcePoints = sourcePointPositions_[frame];
    const PointPositions& initialPoints = initialTargetPointPositions_[frame];
    const PointNormals& sourceNormals = sourcePointNormals_[frame];
    const Vector3 upAxis = definition_.environment.upAxis.normalized();
    const Scalar sourceHeight = definition_.sourceCharacter.height;
    const AdaptiveWeightSettings& adaptive = definition_.adaptiveWeights;
    const Scalar interactionMinimum =
        adaptive.interactionMinimumDistanceRatio * sourceHeight;
    const Scalar interactionMaximum =
        adaptive.interactionMaximumDistanceRatio * sourceHeight;
    const Scalar targetInteractionMinimum =
        adaptive.interactionMinimumDistanceRatio *
        definition_.targetCharacter.height;
    const Scalar targetInteractionMaximum =
        adaptive.interactionMaximumDistanceRatio *
        definition_.targetCharacter.height;
    const Scalar floorMinimum = adaptive.floorMinimumHeightRatio * sourceHeight;
    const Scalar floorMaximum = adaptive.floorMaximumHeightRatio * sourceHeight;
    ContextLossComponents components;

    for (std::size_t relationshipIndex = 0;
         relationshipIndex < definition_.distanceRelationships.size();
         ++relationshipIndex) {
        const DistanceRelationship& relationship =
            definition_.distanceRelationships[relationshipIndex];
        const Vector3 sourceOffset = sourcePoints[relationship.secondPoint] -
            sourcePoints[relationship.firstPoint];
        const Vector3 targetOffset = candidatePoints[relationship.secondPoint] -
            candidatePoints[relationship.firstPoint];
        const Scalar sourceDistance = sourceOffset.length();
        const Scalar sourceAdaptiveWeight =
            adaptive.enabled && relationship.useAdaptiveWeight
                ? clampedProximityWeight(
                      sourceDistance,
                      interactionMinimum,
                      interactionMaximum)
                : 1.0;
        Scalar combinedAdaptiveWeight = sourceAdaptiveWeight;
        if (adaptive.enabled && adaptive.useTargetWeights &&
            relationship.useAdaptiveWeight) {
            const Scalar targetAdaptiveWeight =
                frozenTargetAdaptiveWeights != nullptr
                    ? (*frozenTargetAdaptiveWeights)[relationshipIndex]
                    : clampedProximityWeight(
                          targetOffset.length(),
                          targetInteractionMinimum,
                          targetInteractionMaximum);
            combinedAdaptiveWeight +=
                targetAdaptiveWeightMix_ * targetAdaptiveWeight;
        }
        const Scalar weightedRelationship =
            relationship.weight * combinedAdaptiveWeight *
            combinedAdaptiveWeight;

        if (relationship.useDistance) {
            const Scalar error = targetOffset.length() - sourceDistance;
            components.distance += weightedRelationship * error * error;
        }
        if (relationship.useDirection) {
            const Scalar error = directionError(sourceOffset, targetOffset);
            components.direction += weightedRelationship * error;
        }
        if (relationship.usePenetration) {
            const Scalar sourcePenetration = Vector3::dot(
                sourceNormals[relationship.firstPoint],
                sourceOffset);
            const Scalar targetPenetration = Vector3::dot(
                candidateNormals[relationship.firstPoint],
                targetOffset);
            const Scalar error = targetPenetration - sourcePenetration;
            components.penetration += weightedRelationship * error * error;
        }
    }

    for (std::size_t point = 0; point < candidatePoints.size(); ++point) {
        const Scalar sourcePointHeight =
            Vector3::dot(upAxis, sourcePoints[point]) -
            definition_.environment.sourceGroundHeight;
        const Scalar targetPointHeight =
            Vector3::dot(upAxis, candidatePoints[point]) -
            definition_.environment.targetGroundHeight;
        const Scalar floorWeight = adaptive.enabled
            ? clampedProximityWeight(
                  sourcePointHeight,
                  floorMinimum,
                  floorMaximum)
            : 1.0;
        const Scalar heightError = targetPointHeight - sourcePointHeight;
        const Scalar floorPenetration = std::min(0.0, targetPointHeight);
        components.height +=
            floorWeight * floorWeight * heightError * heightError +
            floorPenetration * floorPenetration;
        components.pointPositionRegularization +=
            squaredDistance(candidatePoints[point], initialPoints[point]);
    }
    return components;
}

std::vector<Scalar> ContextObjective::calculateTargetAdaptiveWeights(
    const PointPositions& candidatePoints) const
{
    std::vector<Scalar> weights(
        definition_.distanceRelationships.size(),
        0.0);
    const AdaptiveWeightSettings& adaptive = definition_.adaptiveWeights;
    if (!adaptive.enabled || !adaptive.useTargetWeights) {
        return weights;
    }
    const Scalar minimum = adaptive.interactionMinimumDistanceRatio *
        definition_.targetCharacter.height;
    const Scalar maximum = adaptive.interactionMaximumDistanceRatio *
        definition_.targetCharacter.height;
    for (std::size_t index = 0;
         index < definition_.distanceRelationships.size();
         ++index) {
        const DistanceRelationship& relationship =
            definition_.distanceRelationships[index];
        if (!relationship.useAdaptiveWeight) {
            continue;
        }
        const Scalar targetDistance =
            (candidatePoints[relationship.secondPoint] -
             candidatePoints[relationship.firstPoint]).length();
        weights[index] = clampedProximityWeight(
            targetDistance,
            minimum,
            maximum);
    }
    return weights;
}

Scalar ContextObjective::evaluateJerkLoss(
    std::size_t firstFrame,
    std::size_t replacementFrame,
    const PointPositions* replacementPositions) const
{
    const auto positions = [this, replacementFrame, replacementPositions](
                               std::size_t frame) -> const PointPositions& {
        return replacementPositions != nullptr && frame == replacementFrame
            ? *replacementPositions
            : candidateTargetPointPositions_[frame];
    };
    const PointPositions& first = positions(firstFrame);
    const PointPositions& second = positions(firstFrame + 1);
    const PointPositions& third = positions(firstFrame + 2);
    const PointPositions& fourth = positions(firstFrame + 3);
    Scalar loss = 0.0;
    for (std::size_t point = 0; point < first.size(); ++point) {
        const Vector3 pointJerk =
            fourth[point] - 3.0 * third[point] + 3.0 * second[point] - first[point];
        const Scalar secondsPerFrame =
            input_.initialTargetPoses.secondsPerFrame;
        loss += pointJerk.length() /
            (secondsPerFrame * secondsPerFrame * secondsPerFrame);
    }
    return loss;
}

ContextLossComponents ContextObjective::evaluateAffectedLossComponents(
    std::size_t frame,
    const PointPositions& candidatePoints,
    const PointNormals& candidateNormals,
    const std::vector<Scalar>& frozenTargetAdaptiveWeights) const
{
    ContextLossComponents components = evaluateSpatialLossComponents(
        frame,
        candidatePoints,
        candidateNormals,
        &frozenTargetAdaptiveWeights);
    if (lossWeights_.pointJerk == 0.0 ||
        candidateTargetPointPositions_.size() < 4) {
        return components;
    }

    const std::size_t firstAffected = frame > 3 ? frame - 3 : 0;
    const std::size_t lastJerkStart = candidateTargetPointPositions_.size() - 4;
    const std::size_t lastAffected = std::min(frame, lastJerkStart);
    for (std::size_t firstFrame = firstAffected;
         firstFrame <= lastAffected;
         ++firstFrame) {
        components.pointJerk += evaluateJerkLoss(
            firstFrame,
            frame,
            &candidatePoints);
    }
    return components;
}

void ContextObjective::evaluatePerturbedFrameGeometry(
    optimization::ConstParameterView parameters,
    std::size_t frame,
    std::size_t degreeToOffset,
    Scalar parameterOffset,
    PointPositions& pointPositions,
    PointNormals& pointNormals) const
{
    LocalPose pose = input_.initialTargetPoses.frames[frame];
    const std::size_t degreesOfFreedomPerFrame =
        definition_.boneRotationDegreesOfFreedom.size();
    for (std::size_t degree = 0;
         degree < degreesOfFreedomPerFrame;
         ++degree) {
        const BoneRotationDegreeOfFreedom& degreeOfFreedom =
            definition_.boneRotationDegreesOfFreedom[degree];
        const Scalar angle =
            parameters[frame * degreesOfFreedomPerFrame + degree] +
            (degree == degreeToOffset ? parameterOffset : 0.0);
        const Quaternion deltaRotation = Quaternion::fromAxisAngle(
            degreeOfFreedom.localAxis,
            angle);
        Quaternion& rotation =
            pose.transforms[degreeOfFreedom.bone].rotation;
        rotation = (rotation * deltaRotation).normalized();
    }

    GlobalPose globalPose;
    calculateGlobalPose(
        definition_.targetCharacter.hierarchy,
        pose,
        globalPose);
    pointPositions.resize(definition_.targetCharacter.contextPoints.size());
    pointNormals.resize(definition_.targetCharacter.contextPoints.size());
    for (std::size_t point = 0;
         point < definition_.targetCharacter.contextPoints.size();
         ++point) {
        const ContextPointDefinition& pointDefinition =
            definition_.targetCharacter.contextPoints[point];
        pointPositions[point] =
            globalPose.transforms[pointDefinition.parentBone].transformPosition(
                pointDefinition.localPosition);
        pointNormals[point] =
            globalPose.transforms[pointDefinition.parentBone]
                .rotation
                .rotateVector(pointDefinition.localNormal)
                .normalized();
    }
}

PoseBatch ContextObjective::createTargetPoses(
    optimization::ConstParameterView parameters) const
{
    PoseBatch result;
    applyParameters(parameters, result);
    return result;
}

void ContextObjective::applyParameters(
    optimization::ConstParameterView parameters,
    PoseBatch& targetPoses) const
{
    if (parameters.size() != parameterCount()) {
        throw std::invalid_argument(
            "Context parameter count must equal frame count times degree-of-freedom count");
    }

    targetPoses = input_.initialTargetPoses;
    const std::size_t degreesOfFreedomPerFrame =
        definition_.boneRotationDegreesOfFreedom.size();

    for (std::size_t frame = 0; frame < targetPoses.frames.size(); ++frame) {
        for (std::size_t degree = 0;
             degree < degreesOfFreedomPerFrame;
             ++degree) {
            const BoneRotationDegreeOfFreedom& degreeOfFreedom =
                definition_.boneRotationDegreesOfFreedom[degree];
            const Scalar angle =
                parameters[frame * degreesOfFreedomPerFrame + degree];
            const Quaternion deltaRotation = Quaternion::fromAxisAngle(
                degreeOfFreedom.localAxis,
                angle);

            Quaternion& rotation =
                targetPoses.frames[frame]
                    .transforms[degreeOfFreedom.bone]
                    .rotation;
            rotation = (rotation * deltaRotation).normalized();
        }
    }
}

void ContextObjective::evaluatePointGeometry(
    const CharacterDefinition& character,
    const PoseBatch& poses,
    std::vector<GlobalPose>& globalPoses,
    std::vector<PointPositions>& pointPositions,
    std::vector<PointNormals>& pointNormals) const
{
    globalPoses.resize(poses.frames.size());
    pointPositions.resize(poses.frames.size());
    pointNormals.resize(poses.frames.size());

    for (std::size_t frame = 0; frame < poses.frames.size(); ++frame) {
        calculateGlobalPose(
            character.hierarchy,
            poses.frames[frame],
            globalPoses[frame]);

        PointPositions& framePoints = pointPositions[frame];
        PointNormals& frameNormals = pointNormals[frame];
        framePoints.resize(character.contextPoints.size());
        frameNormals.resize(character.contextPoints.size());
        for (std::size_t point = 0;
             point < character.contextPoints.size();
             ++point) {
            const ContextPointDefinition& definition =
                character.contextPoints[point];
            framePoints[point] =
                globalPoses[frame]
                    .transforms[definition.parentBone]
                    .transformPosition(definition.localPosition);
            frameNormals[point] =
                globalPoses[frame]
                    .transforms[definition.parentBone]
                    .rotation
                    .rotateVector(definition.localNormal)
                    .normalized();
        }
    }
}

} // namespace context_retargeting::context
