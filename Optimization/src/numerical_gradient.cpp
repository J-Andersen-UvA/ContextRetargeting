#include "context_retargeting/optimization/numerical_gradient.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace context_retargeting::optimization {
namespace {

void validateSettings(const CentralDifferenceSettings& settings)
{
    if (!std::isfinite(settings.absoluteStep) ||
        !std::isfinite(settings.relativeStep) ||
        settings.absoluteStep < 0.0 ||
        settings.relativeStep < 0.0 ||
        (settings.absoluteStep == 0.0 && settings.relativeStep == 0.0)) {
        throw std::invalid_argument(
            "Central-difference steps must be finite, non-negative, and not both zero");
    }
}

} // namespace

void centralDifferenceGradient(
    const LossFunction& loss,
    ConstParameterView parameters,
    ParameterView gradientOut,
    const CentralDifferenceSettings& settings)
{
    if (!loss) {
        throw std::invalid_argument("Loss function must not be empty");
    }
    if (gradientOut.size() != parameters.size()) {
        throw std::invalid_argument(
            "Gradient output size must match parameter count");
    }
    validateSettings(settings);

    ParameterVector evaluationParameters(
        parameters.begin(),
        parameters.end());

    for (std::size_t index = 0; index < evaluationParameters.size(); ++index) {
        const Scalar original = evaluationParameters[index];
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

        evaluationParameters[index] = original + step;
        const Scalar lossPlus = loss(evaluationParameters);

        evaluationParameters[index] = original - step;
        const Scalar lossMinus = loss(evaluationParameters);

        evaluationParameters[index] = original;
        gradientOut[index] = (lossPlus - lossMinus) / (2.0 * step);
    }
}

GradientFunction makeCentralDifferenceGradient(
    LossFunction loss,
    CentralDifferenceSettings settings)
{
    if (!loss) {
        throw std::invalid_argument("Loss function must not be empty");
    }
    validateSettings(settings);

    return [loss = std::move(loss), settings](
               ConstParameterView parameters,
               ParameterView gradientOut) {
        centralDifferenceGradient(loss, parameters, gradientOut, settings);
    };
}

} // namespace context_retargeting::optimization
