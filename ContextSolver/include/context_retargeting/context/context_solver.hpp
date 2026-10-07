#pragma once

#include "context_retargeting/context/context_objective.hpp"
#include "context_retargeting/optimization/adam.hpp"
#include "context_retargeting/optimization/minimize.hpp"
#include "context_retargeting/optimization/numerical_gradient.hpp"

namespace context_retargeting::context {

struct ContextSolverSettings
{
    ContextLossWeights lossWeights;
    optimization::CentralDifferenceSettings numericalGradient;
    optimization::AdamSettings adam;
    optimization::OptimizationSettings optimization;
};

struct ContextSolveResult
{
    PoseBatch targetPoses;
    ContextLossComponents initialLoss;
    ContextLossComponents finalLoss;
    optimization::OptimizationResult optimization;
};

[[nodiscard]] ContextSolveResult solveContextBatch(
    const ContextDefinition& definition,
    const ContextBatchInput& input,
    const ContextSolverSettings& settings = {});

} // namespace context_retargeting::context
