#pragma once

#include "context_retargeting/optimization/adam.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace context_retargeting::optimization {

using OptimizationProgressFunction =
    std::function<void(std::size_t iteration, Scalar loss)>;

enum class StopReason
{
    MaxIterations,
    GradientTolerance,
    LossImprovement,
    NonFiniteLoss,
    NonFiniteGradient
};

struct OptimizationSettings
{
    // This is the maximum number of Adam parameter updates.
    std::size_t maxIterations = 100;

    // Zero disables gradient-based early stopping.
    Scalar gradientTolerance = 0.0;

    // Zero disables relative-loss-improvement early stopping.
    Scalar minimumRelativeLossImprovement = 0.0;

    std::size_t lossImprovementPatience = 10;

    // Disable this when the objective is intentionally scheduled between
    // iterations and losses from different iterations are not comparable.
    bool returnBestParameters = true;

    OptimizationProgressFunction progress;
};

struct OptimizationResult
{
    ParameterVector parameters;
    Scalar loss = 0.0;
    std::size_t iterations = 0;
    StopReason stopReason = StopReason::MaxIterations;
    std::vector<Scalar> lossHistory;
};

// Coordinates loss evaluation, an injected gradient strategy, and Adam. The
// returned loss always corresponds to the returned parameters.
[[nodiscard]] OptimizationResult minimizeWithAdam(
    ParameterVector initialParameters,
    const LossFunction& loss,
    const GradientFunction& gradient,
    AdamSettings adamSettings = {},
    OptimizationSettings optimizationSettings = {});

} // namespace context_retargeting::optimization
