#include "context_retargeting/optimization/adam.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace context_retargeting::optimization {
namespace {

void validateSettings(const AdamSettings& settings)
{
    if (!std::isfinite(settings.learningRate) || settings.learningRate <= 0.0) {
        throw std::invalid_argument("Adam learning rate must be finite and positive");
    }
    if (!std::isfinite(settings.beta1) ||
        settings.beta1 < 0.0 || settings.beta1 >= 1.0) {
        throw std::invalid_argument("Adam beta1 must be in [0, 1)");
    }
    if (!std::isfinite(settings.beta2) ||
        settings.beta2 < 0.0 || settings.beta2 >= 1.0) {
        throw std::invalid_argument("Adam beta2 must be in [0, 1)");
    }
    if (!std::isfinite(settings.epsilon) || settings.epsilon <= 0.0) {
        throw std::invalid_argument("Adam epsilon must be finite and positive");
    }
}

} // namespace

Adam::Adam(std::size_t parameterCount, AdamSettings settings)
    : settings_(settings),
      momentum_(parameterCount, 0.0),
      squaredGradientAverage_(parameterCount, 0.0)
{
    validateSettings(settings_);
}

void Adam::step(ParameterView parameters, ConstParameterView gradient)
{
    if (parameters.size() != momentum_.size() ||
        gradient.size() != momentum_.size()) {
        throw std::invalid_argument(
            "Adam parameter and gradient sizes must match its configured dimension");
    }

    ++iteration_;
    beta1Power_ *= settings_.beta1;
    beta2Power_ *= settings_.beta2;

    const Scalar momentumBiasCorrection = 1.0 - beta1Power_;
    const Scalar squaredGradientAverageBiasCorrection = 1.0 - beta2Power_;

    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const Scalar value = gradient[index];
        momentum_[index] =
            settings_.beta1 * momentum_[index] +
            (1.0 - settings_.beta1) * value;
        squaredGradientAverage_[index] =
            settings_.beta2 * squaredGradientAverage_[index] +
            (1.0 - settings_.beta2) * value * value;

        const Scalar biasCorrectedMomentum =
            momentum_[index] / momentumBiasCorrection;
        const Scalar biasCorrectedSquaredGradientAverage =
            squaredGradientAverage_[index] /
            squaredGradientAverageBiasCorrection;

        parameters[index] -= settings_.learningRate * biasCorrectedMomentum /
            (std::sqrt(biasCorrectedSquaredGradientAverage) + settings_.epsilon);
    }
}

void Adam::reset() noexcept
{
    std::fill(momentum_.begin(), momentum_.end(), 0.0);
    std::fill(
        squaredGradientAverage_.begin(),
        squaredGradientAverage_.end(),
        0.0);
    iteration_ = 0;
    beta1Power_ = 1.0;
    beta2Power_ = 1.0;
}

std::size_t Adam::iteration() const noexcept
{
    return iteration_;
}

std::size_t Adam::parameterCount() const noexcept
{
    return momentum_.size();
}

} // namespace context_retargeting::optimization
