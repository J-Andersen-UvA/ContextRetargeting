#include "test_support.hpp"

#include "context_retargeting/context/context_objective.hpp"
#include "context_retargeting/optimization/numerical_gradient.hpp"

#include <cmath>
#include <numbers>

using namespace context_retargeting::context;
using context_retargeting::optimization::ParameterVector;

namespace {

ContextDefinition makeDefinition()
{
    const TransformHierarchy hierarchy{{
        {invalidBoneIndex},
        {0}}};

    CharacterDefinition character;
    character.hierarchy = hierarchy;
    character.contextPoints = {
        {0, {0.0, 1.0, 0.0}},
        {1, {1.0, 0.0, 0.0}}};

    ContextDefinition definition;
    definition.sourceCharacter = character;
    definition.targetCharacter = character;
    definition.distanceRelationships = {{0, 1, 1.0}};
    definition.boneRotationDegreesOfFreedom = {
        {1, {0.0, 0.0, 1.0}}};
    definition.adaptiveWeights.enabled = false;
    return definition;
}

ContextLossWeights distanceAndRegularizationWeights()
{
    ContextLossWeights result;
    result.direction = 0.0;
    result.penetration = 0.0;
    result.height = 0.0;
    return result;
}

LocalPose makePose(Scalar childAngle)
{
    LocalPose pose;
    pose.transforms = {
        {Quaternion::identity(), {0.0, 0.0, 0.0}},
        {Quaternion::fromAxisAngle({0.0, 0.0, 1.0}, childAngle),
         {1.0, 0.0, 0.0}}};
    return pose;
}

ContextBatchInput makeInput(
    const std::vector<Scalar>& sourceAngles,
    const std::vector<Scalar>& targetAngles)
{
    ContextBatchInput input;
    input.sourcePoses.secondsPerFrame = 1.0 / 30.0;
    input.initialTargetPoses.secondsPerFrame = 1.0 / 30.0;
    for (const Scalar angle : sourceAngles) {
        input.sourcePoses.frames.push_back(makePose(angle));
    }
    for (const Scalar angle : targetAngles) {
        input.initialTargetPoses.frames.push_back(makePose(angle));
    }
    return input;
}

} // namespace

CRT_TEST("zero parameters reproduce the initial target poses")
{
    const ContextDefinition definition = makeDefinition();
    const ContextBatchInput input = makeInput({std::numbers::pi / 2.0}, {0.0});
    ContextObjective objective(
        definition,
        input,
        distanceAndRegularizationWeights());
    const ParameterVector parameters(objective.parameterCount(), 0.0);

    const PoseBatch result = objective.createTargetPoses(parameters);

    CRT_REQUIRE(result.frames[0].transforms[1].rotation ==
        input.initialTargetPoses.frames[0].transforms[1].rotation);
    const ContextLossComponents loss = objective.evaluateLossComponents(parameters);
    CRT_REQUIRE(loss.pointPositionRegularization == 0.0);
}

CRT_TEST("distance loss compares corresponding source and target point distances")
{
    const ContextDefinition definition = makeDefinition();
    const ContextBatchInput input = makeInput({std::numbers::pi / 2.0}, {0.0});
    ContextObjective objective(
        definition,
        input,
        distanceAndRegularizationWeights());

    const ParameterVector matchingParameters{std::numbers::pi / 2.0};
    const ContextLossComponents matchingLoss =
        objective.evaluateLossComponents(matchingParameters);

    CRT_REQUIRE_NEAR(matchingLoss.distance, 0.0, 1.0e-12);
    CRT_REQUIRE_NEAR(
        matchingLoss.pointPositionRegularization,
        2.0,
        1.0e-12);
}

CRT_TEST("each frame has an independent candidate segment")
{
    const ContextDefinition definition = makeDefinition();
    const ContextBatchInput input = makeInput(
        {std::numbers::pi / 2.0, -std::numbers::pi / 2.0},
        {0.0, 0.0});
    ContextLossWeights weights = distanceAndRegularizationWeights();
    weights.pointPositionRegularization = 0.0;
    ContextObjective objective(definition, input, weights);

    CRT_REQUIRE(objective.parameterCount() == 2);
    const ParameterVector parameters{
        std::numbers::pi / 2.0,
        -std::numbers::pi / 2.0};
    const ContextLossComponents loss = objective.evaluateLossComponents(parameters);

    CRT_REQUIRE_NEAR(loss.distance, 0.0, 1.0e-12);
}

CRT_TEST("context objective validates candidate size")
{
    const ContextDefinition definition = makeDefinition();
    const ContextBatchInput input = makeInput({0.0}, {0.0});
    ContextObjective objective(
        definition,
        input,
        distanceAndRegularizationWeights());
    const ParameterVector wrongSize;

    CRT_REQUIRE_THROWS(
        std::invalid_argument,
        objective.evaluateLoss(wrongSize));
}

CRT_TEST("direction and penetration descriptors compare point geometry")
{
    ContextDefinition definition = makeDefinition();
    const ContextBatchInput input = makeInput({std::numbers::pi / 2.0}, {0.0});
    ContextLossWeights weights;
    weights.distance = 0.0;
    weights.height = 0.0;
    weights.pointPositionRegularization = 0.0;
    ContextObjective objective(definition, input, weights);

    const ParameterVector initialParameters{0.0};
    const ContextLossComponents initialLoss =
        objective.evaluateLossComponents(initialParameters);
    const Scalar cosineDistance = 1.0 - 2.0 / std::sqrt(5.0);
    CRT_REQUIRE_NEAR(
        initialLoss.direction,
        cosineDistance * cosineDistance,
        1.0e-12);
    CRT_REQUIRE(initialLoss.penetration > 0.0);

    const ParameterVector matchingParameters{std::numbers::pi / 2.0};
    const ContextLossComponents matchingLoss =
        objective.evaluateLossComponents(matchingParameters);
    CRT_REQUIRE_NEAR(matchingLoss.direction, 0.0, 1.0e-12);
    CRT_REQUIRE_NEAR(matchingLoss.penetration, 0.0, 1.0e-12);
}

CRT_TEST("height descriptor compares ground-relative point heights")
{
    ContextDefinition definition = makeDefinition();
    definition.distanceRelationships.clear();
    definition.adaptiveWeights.enabled = false;
    definition.environment.upAxis = {0.0, 1.0, 0.0};
    const ContextBatchInput input = makeInput({std::numbers::pi / 2.0}, {0.0});
    ContextLossWeights weights;
    weights.distance = 0.0;
    weights.direction = 0.0;
    weights.penetration = 0.0;
    weights.pointPositionRegularization = 0.0;
    ContextObjective objective(definition, input, weights);

    const ContextLossComponents initialLoss =
        objective.evaluateLossComponents(ParameterVector{0.0});
    const ContextLossComponents matchingLoss =
        objective.evaluateLossComponents(ParameterVector{std::numbers::pi / 2.0});
    CRT_REQUIRE(initialLoss.height > 0.0);
    CRT_REQUIRE_NEAR(matchingLoss.height, 0.0, 1.0e-12);
}

CRT_TEST("adaptive relationship weight ignores distant source points")
{
    ContextDefinition definition = makeDefinition();
    definition.sourceCharacter.height = 1.0;
    definition.targetCharacter.height = 1.0;
    definition.adaptiveWeights.enabled = true;
    const ContextBatchInput input = makeInput({std::numbers::pi / 2.0}, {0.0});
    ContextLossWeights weights;
    weights.direction = 0.0;
    weights.penetration = 0.0;
    weights.height = 0.0;
    weights.pointPositionRegularization = 0.0;
    ContextObjective objective(definition, input, weights);

    const ContextLossComponents loss =
        objective.evaluateLossComponents(ParameterVector{0.0});
    CRT_REQUIRE_NEAR(loss.distance, 0.0, 1.0e-12);
}

CRT_TEST("target adaptive weight activates a target-only close relationship")
{
    ContextDefinition definition = makeDefinition();
    definition.sourceCharacter.height = 1.0;
    definition.targetCharacter.height = 20.0;
    definition.adaptiveWeights.enabled = true;
    definition.adaptiveWeights.useTargetWeights = true;
    const ContextBatchInput input = makeInput({std::numbers::pi / 2.0}, {0.0});
    ContextLossWeights weights;
    weights.direction = 0.0;
    weights.penetration = 0.0;
    weights.height = 0.0;
    weights.pointPositionRegularization = 0.0;
    ContextObjective objective(definition, input, weights);
    const ParameterVector parameters{0.0};

    const ContextLossComponents sourceOnly =
        objective.evaluateLossComponents(parameters);
    objective.setTargetAdaptiveWeightMix(1.0);
    const ContextLossComponents combined =
        objective.evaluateLossComponents(parameters);

    CRT_REQUIRE_NEAR(sourceOnly.distance, 0.0, 1.0e-12);
    CRT_REQUIRE(combined.distance > 0.0);
}

CRT_TEST("point jerk loss uses four consecutive frames")
{
    ContextDefinition definition = makeDefinition();
    definition.distanceRelationships.clear();
    const ContextBatchInput input = makeInput(
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, std::numbers::pi / 2.0});
    ContextLossWeights weights;
    weights.distance = 0.0;
    weights.direction = 0.0;
    weights.penetration = 0.0;
    weights.height = 0.0;
    weights.pointPositionRegularization = 0.0;
    weights.pointJerk = 1.0;
    ContextObjective objective(definition, input, weights);

    const ParameterVector parameters(objective.parameterCount(), 0.0);
    const ContextLossComponents loss = objective.evaluateLossComponents(parameters);
    CRT_REQUIRE_NEAR(
        loss.pointJerk,
        std::sqrt(2.0) * 30.0 * 30.0 * 30.0,
        1.0e-8);
}

CRT_TEST("point jerk loss uses physical time units")
{
    ContextDefinition definition = makeDefinition();
    definition.distanceRelationships.clear();
    ContextBatchInput input = makeInput(
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, std::numbers::pi / 2.0});
    ContextLossWeights weights;
    weights.distance = 0.0;
    weights.direction = 0.0;
    weights.penetration = 0.0;
    weights.height = 0.0;
    weights.pointPositionRegularization = 0.0;
    weights.pointJerk = 1.0;

    ContextObjective thirtyFpsObjective(definition, input, weights);
    const ParameterVector parameters(thirtyFpsObjective.parameterCount(), 0.0);
    const Scalar thirtyFpsJerk =
        thirtyFpsObjective.evaluateLossComponents(parameters).pointJerk;

    input.sourcePoses.secondsPerFrame = 1.0 / 60.0;
    input.initialTargetPoses.secondsPerFrame = 1.0 / 60.0;
    ContextObjective sixtyFpsObjective(definition, input, weights);
    const Scalar sixtyFpsJerk =
        sixtyFpsObjective.evaluateLossComponents(parameters).pointJerk;

    CRT_REQUIRE_NEAR(sixtyFpsJerk, thirtyFpsJerk * 8.0, 1.0e-7);
}

CRT_TEST("sparse context gradient matches generic central differences")
{
    ContextDefinition definition = makeDefinition();
    definition.adaptiveWeights.enabled = false;
    definition.boneRotationDegreesOfFreedom.push_back(
        {1, {0.0, 1.0, 0.0}});
    const ContextBatchInput input = makeInput(
        {0.2, 0.4, 0.1, -0.3, -0.1},
        {0.0, 0.1, -0.1, 0.2, 0.0});
    ContextLossWeights weights;
    weights.distance = 1.0;
    weights.direction = 0.7;
    weights.penetration = 0.4;
    weights.height = 0.3;
    weights.pointPositionRegularization = 0.2;
    weights.pointJerk = 0.05;
    ContextObjective objective(definition, input, weights);
    const ParameterVector parameters{
        0.02, -0.01,
        -0.03, 0.02,
        0.01, 0.03,
        0.04, -0.02,
        -0.02, 0.01};
    ParameterVector genericGradient(parameters.size(), 0.0);
    ParameterVector sparseGradient(parameters.size(), 0.0);
    ContextGradientVectors gradientVectors;
    context_retargeting::optimization::CentralDifferenceSettings settings;
    settings.absoluteStep = 1.0e-5;
    settings.relativeStep = 1.0e-5;

    context_retargeting::optimization::centralDifferenceGradient(
        [&objective](context_retargeting::optimization::ConstParameterView values) {
            return objective.evaluateLoss(values);
        },
        parameters,
        genericGradient,
        settings);
    const ContextGradientMagnitudes gradientMagnitudes =
        objective.evaluateCentralDifferenceGradient(
        parameters,
        sparseGradient,
        settings,
        &gradientVectors);

    for (std::size_t index = 0; index < parameters.size(); ++index) {
        CRT_REQUIRE_NEAR(sparseGradient[index], genericGradient[index], 1.0e-7);
        CRT_REQUIRE_NEAR(
            gradientVectors.total[index],
            gradientVectors.distance[index] +
                gradientVectors.direction[index] +
                gradientVectors.penetration[index] +
                gradientVectors.height[index] +
                gradientVectors.pointPositionRegularization[index] +
                gradientVectors.pointJerk[index],
            1.0e-12);
    }
    CRT_REQUIRE(gradientMagnitudes.total > 0.0);
    CRT_REQUIRE(gradientMagnitudes.distance > 0.0);
    CRT_REQUIRE(gradientMagnitudes.pointJerk > 0.0);
}
