#pragma once

#include <functional>
#include <span>
#include <vector>

namespace context_retargeting::optimization {

using Scalar = double;
using ParameterVector = std::vector<Scalar>;
using ParameterView = std::span<Scalar>;
using ConstParameterView = std::span<const Scalar>;

using LossFunction = std::function<Scalar(ConstParameterView)>;
using GradientFunction =
    std::function<void(ConstParameterView, ParameterView)>;

} // namespace context_retargeting::optimization
