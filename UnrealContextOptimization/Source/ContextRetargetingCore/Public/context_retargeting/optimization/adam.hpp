#pragma once

#include "context_retargeting/optimization/types.hpp"

#include <cstddef>

namespace context_retargeting::optimization {

struct AdamSettings
{
    Scalar learningRate = 1.0e-3;
    Scalar beta1 = 0.9;
    Scalar beta2 = 0.999;
    Scalar epsilon = 1.0e-8;
};

class CONTEXTRETARGETINGCORE_API Adam
{
public:
    explicit Adam(
        std::size_t parameterCount,
        AdamSettings settings = {});

    // Updates parameters in place from a gradient supplied by any strategy.
    // Internally, momentum is m_t, squaredGradientAverage is v_t, and their
    // bias-corrected values are m_hat_t and v_hat_t respectively.
    void step(ParameterView parameters, ConstParameterView gradient);

    void reset() noexcept;

    [[nodiscard]] std::size_t iteration() const noexcept;
    [[nodiscard]] std::size_t parameterCount() const noexcept;

private:
    AdamSettings settings_;
    ParameterVector momentum_;               // m_t
    ParameterVector squaredGradientAverage_; // v_t
    std::size_t iteration_ = 0;
    Scalar beta1Power_ = 1.0;
    Scalar beta2Power_ = 1.0;
};

} // namespace context_retargeting::optimization
