#include "context_retargeting/optimization/minimize.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace context_retargeting::optimization {
namespace {

bool allFinite(ConstParameterView values)
{
    for (const Scalar value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

Scalar euclideanNorm(ConstParameterView values)
{
    // hypot avoids the needless overflow/underflow of sum(value * value).
    Scalar norm = 0.0;
    for (const Scalar value : values) {
        norm = std::hypot(norm, value);
    }
    return norm;
}

void validateSettings(const OptimizationSettings& settings)
{
    if (!std::isfinite(settings.gradientTolerance) ||
        settings.gradientTolerance < 0.0 ||
        !std::isfinite(settings.minimumRelativeLossImprovement) ||
        settings.minimumRelativeLossImprovement < 0.0 ||
        (settings.minimumRelativeLossImprovement > 0.0 &&
         settings.lossImprovementPatience == 0)) {
        throw std::invalid_argument(
            "Optimization stopping settings are invalid");
    }
}

} // namespace

OptimizationResult minimizeWithAdam(
    ParameterVector initialParameters,
    const LossFunction& loss,
    const GradientFunction& gradient,
    AdamSettings adamSettings,
    OptimizationSettings optimizationSettings)
{
    if (!loss) {
        throw std::invalid_argument("Loss function must not be empty");
    }
    if (!gradient) {
        throw std::invalid_argument("Gradient function must not be empty");
    }
    validateSettings(optimizationSettings);

    OptimizationResult result;
    result.parameters = std::move(initialParameters);
    result.loss = loss(result.parameters);
    result.lossHistory.push_back(result.loss);

    if (!std::isfinite(result.loss)) {
        result.stopReason = StopReason::NonFiniteLoss;
        return result;
    }
    if (optimizationSettings.progress) {
        optimizationSettings.progress(0, result.loss);
    }
    ParameterVector bestParameters = result.parameters;
    Scalar bestLoss = result.loss;

    const auto restoreBestResult = [&result, &bestParameters, &bestLoss]() {
        result.parameters = bestParameters;
        result.loss = bestLoss;
    };

    Adam adam(result.parameters.size(), adamSettings);
    ParameterVector gradientValues(result.parameters.size(), 0.0);

    for (std::size_t update = 0;
         update < optimizationSettings.maxIterations;
         ++update) {
        gradient(result.parameters, gradientValues);

        if (!allFinite(gradientValues)) {
            result.stopReason = StopReason::NonFiniteGradient;
            restoreBestResult();
            return result;
        }

        if (optimizationSettings.gradientTolerance > 0.0 &&
            euclideanNorm(gradientValues) <=
                optimizationSettings.gradientTolerance) {
            result.stopReason = StopReason::GradientTolerance;
            restoreBestResult();
            return result;
        }

        adam.step(result.parameters, gradientValues);
        ++result.iterations;

        result.loss = loss(result.parameters);
        result.lossHistory.push_back(result.loss);
        if (!std::isfinite(result.loss)) {
            result.stopReason = StopReason::NonFiniteLoss;
            restoreBestResult();
            return result;
        }
        if (optimizationSettings.progress) {
            optimizationSettings.progress(result.iterations, result.loss);
        }
        if (result.loss < bestLoss) {
            bestLoss = result.loss;
            bestParameters = result.parameters;
        }

        if (optimizationSettings.minimumRelativeLossImprovement > 0.0 &&
            result.iterations >= optimizationSettings.lossImprovementPatience) {
            const Scalar referenceLoss = result.lossHistory[
                result.lossHistory.size() - 1 -
                optimizationSettings.lossImprovementPatience];
            const Scalar scale = std::max(std::abs(referenceLoss), 1.0);
            const Scalar relativeImprovement =
                (referenceLoss - result.loss) / scale;
            if (relativeImprovement <
                optimizationSettings.minimumRelativeLossImprovement) {
                result.stopReason = StopReason::LossImprovement;
                restoreBestResult();
                return result;
            }
        }
    }

    result.stopReason = StopReason::MaxIterations;
    if (optimizationSettings.returnBestParameters) {
        restoreBestResult();
    }
    return result;
}

} // namespace context_retargeting::optimization
