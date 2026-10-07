#include "context_retargeting/context/context_solver.hpp"

#include <algorithm>
#include <utility>

namespace context_retargeting::context {

ContextSolveResult solveContextBatch(
    const ContextDefinition& definition,
    const ContextBatchInput& input,
    const ContextSolverSettings& settings)
{
    ContextObjective objective(definition, input, settings.lossWeights);
    const bool scheduleTargetAdaptiveWeights =
        definition.adaptiveWeights.enabled &&
        definition.adaptiveWeights.useTargetWeights;
    if (scheduleTargetAdaptiveWeights &&
        settings.optimization.maxIterations <= 1) {
        objective.setTargetAdaptiveWeightMix(1.0);
    }
    optimization::ParameterVector initialParameters(
        objective.parameterCount(),
        0.0);

    ContextSolveResult result;
    result.initialLoss = objective.evaluateLossComponents(initialParameters);

    const optimization::LossFunction loss =
        [&objective](optimization::ConstParameterView parameters) {
            return objective.evaluateLoss(parameters);
        };
    const optimization::GradientFunction gradient =
        [&objective, gradientSettings = settings.numericalGradient](
            optimization::ConstParameterView parameters,
            optimization::ParameterView gradientOut) {
            static_cast<void>(objective.evaluateCentralDifferenceGradient(
                parameters,
                gradientOut,
                gradientSettings));
        };

    optimization::OptimizationSettings optimizationSettings =
        settings.optimization;
    if (scheduleTargetAdaptiveWeights) {
        const optimization::OptimizationProgressFunction userProgress =
            optimizationSettings.progress;
        const std::size_t maxIterations = optimizationSettings.maxIterations;
        optimizationSettings.returnBestParameters = false;
        optimizationSettings.gradientTolerance = 0.0;
        optimizationSettings.minimumRelativeLossImprovement = 0.0;
        optimizationSettings.progress =
            [&objective, userProgress, maxIterations](
                std::size_t iteration,
                Scalar loss) {
                const Scalar targetWeightMix = maxIterations <= 1
                    ? 1.0
                    : std::min(
                          static_cast<Scalar>(iteration) /
                              static_cast<Scalar>(maxIterations - 1),
                          1.0);
                objective.setTargetAdaptiveWeightMix(targetWeightMix);
                if (userProgress) {
                    userProgress(iteration, loss);
                }
            };
    }

    result.optimization = optimization::minimizeWithAdam(
        std::move(initialParameters),
        loss,
        gradient,
        settings.adam,
        std::move(optimizationSettings));

    if (scheduleTargetAdaptiveWeights) {
        objective.setTargetAdaptiveWeightMix(1.0);
        const optimization::ParameterVector comparisonInitialParameters(
            objective.parameterCount(),
            0.0);
        result.initialLoss = objective.evaluateLossComponents(
            comparisonInitialParameters);
    }
    result.finalLoss =
        objective.evaluateLossComponents(result.optimization.parameters);
    result.targetPoses =
        objective.createTargetPoses(result.optimization.parameters);
    return result;
}

} // namespace context_retargeting::context
