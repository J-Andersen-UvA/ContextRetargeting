#pragma once

#include "context_retargeting/optimization/types.hpp"

namespace context_retargeting::optimization {

struct CentralDifferenceSettings
{
    Scalar absoluteStep = 1.0e-6;
    Scalar relativeStep = 1.0e-6;
};

// Estimates the gradient without modifying parameters. The loss function is
// evaluated exactly twice per parameter.
CONTEXTRETARGETINGCORE_API void centralDifferenceGradient(
    const LossFunction& loss,
    ConstParameterView parameters,
    ParameterView gradientOut,
    const CentralDifferenceSettings& settings = {});

// Wraps centralDifferenceGradient as an interchangeable gradient strategy.
[[nodiscard]] CONTEXTRETARGETINGCORE_API GradientFunction makeCentralDifferenceGradient(
    LossFunction loss,
    CentralDifferenceSettings settings = {});

} // namespace context_retargeting::optimization
