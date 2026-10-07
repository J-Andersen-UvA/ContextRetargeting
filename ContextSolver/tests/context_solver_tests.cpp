#include "test_support.hpp"

#include "context_retargeting/context/context_solver.hpp"

#include <numbers>

using namespace context_retargeting::context;

namespace {

ContextDefinition makeSolverDefinition()
{
    CharacterDefinition character;
    character.hierarchy.bones = {
        {invalidBoneIndex},
        {0}};
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

LocalPose makeSolverPose(Scalar angle)
{
    return {{
        {Quaternion::identity(), {0.0, 0.0, 0.0}},
        {Quaternion::fromAxisAngle({0.0, 0.0, 1.0}, angle),
         {1.0, 0.0, 0.0}}}};
}

} // namespace

CRT_TEST("context solver reduces batched distance loss")
{
    const ContextDefinition definition = makeSolverDefinition();
    ContextBatchInput input;
    input.sourcePoses.secondsPerFrame = 1.0 / 30.0;
    input.initialTargetPoses.secondsPerFrame = 1.0 / 30.0;
    input.sourcePoses.frames = {
        makeSolverPose(std::numbers::pi / 2.0),
        makeSolverPose(-std::numbers::pi / 4.0)};
    input.initialTargetPoses.frames = {
        makeSolverPose(0.0),
        makeSolverPose(0.0)};

    ContextSolverSettings settings;
    settings.lossWeights.distance = 1.0;
    settings.lossWeights.direction = 0.0;
    settings.lossWeights.penetration = 0.0;
    settings.lossWeights.height = 0.0;
    settings.lossWeights.pointPositionRegularization = 0.01;
    settings.adam.learningRate = 0.1;
    settings.optimization.maxIterations = 250;

    const ContextSolveResult result =
        solveContextBatch(definition, input, settings);

    CRT_REQUIRE(result.finalLoss.weightedTotal(settings.lossWeights) <
        result.initialLoss.weightedTotal(settings.lossWeights));
    CRT_REQUIRE(result.finalLoss.distance < result.initialLoss.distance);
    CRT_REQUIRE(result.targetPoses.frames.size() == 2);
    CRT_REQUIRE(result.optimization.parameters.size() == 2);
    CRT_REQUIRE(result.optimization.parameters[0] > 0.0);
    CRT_REQUIRE(result.optimization.parameters[1] < 0.0);
}

CRT_TEST("context solver schedules target adaptive weights")
{
    ContextDefinition definition = makeSolverDefinition();
    definition.sourceCharacter.height = 1.0;
    definition.targetCharacter.height = 20.0;
    definition.adaptiveWeights.enabled = true;
    definition.adaptiveWeights.useTargetWeights = true;
    ContextBatchInput input;
    input.sourcePoses.secondsPerFrame = 1.0 / 30.0;
    input.initialTargetPoses.secondsPerFrame = 1.0 / 30.0;
    input.sourcePoses.frames = {makeSolverPose(std::numbers::pi / 2.0)};
    input.initialTargetPoses.frames = {makeSolverPose(0.0)};

    ContextSolverSettings settings;
    settings.lossWeights.direction = 0.0;
    settings.lossWeights.penetration = 0.0;
    settings.lossWeights.height = 0.0;
    settings.lossWeights.pointPositionRegularization = 0.0;
    settings.adam.learningRate = 0.05;
    settings.optimization.maxIterations = 80;
    settings.optimization.minimumRelativeLossImprovement = 1.0;
    settings.optimization.lossImprovementPatience = 1;

    const ContextSolveResult result =
        solveContextBatch(definition, input, settings);

    CRT_REQUIRE(result.optimization.iterations == 80);
    CRT_REQUIRE(result.optimization.parameters[0] > 0.1);
    CRT_REQUIRE(result.finalLoss.distance > 0.0);
}
