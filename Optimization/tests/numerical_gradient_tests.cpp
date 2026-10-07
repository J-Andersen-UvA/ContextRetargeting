#include "test_support.hpp"

#include "context_retargeting/optimization/numerical_gradient.hpp"

#include <limits>
#include <stdexcept>

using namespace context_retargeting::optimization;

CRT_TEST("central difference differentiates a one-dimensional quadratic")
{
    const LossFunction loss = [](ConstParameterView x) { return x[0] * x[0]; };

    for (const Scalar value : {-3.0, 0.0, 4.5}) {
        const ParameterVector parameters{value};
        ParameterVector gradient(1);
        centralDifferenceGradient(loss, parameters, gradient);
        CRT_REQUIRE_NEAR(gradient[0], 2.0 * value, 1.0e-7);
    }
}

CRT_TEST("central difference differentiates multiple dimensions and cross terms")
{
    const LossFunction loss = [](ConstParameterView x) {
        return x[0] * x[1] + 3.0 * x[0] * x[0] + 2.0 * x[2] * x[2];
    };
    const ParameterVector parameters{2.0, -4.0, 3.0};
    ParameterVector gradient(3);

    centralDifferenceGradient(loss, parameters, gradient);

    CRT_REQUIRE_NEAR(gradient[0], 8.0, 1.0e-7);
    CRT_REQUIRE_NEAR(gradient[1], 2.0, 1.0e-7);
    CRT_REQUIRE_NEAR(gradient[2], 12.0, 1.0e-7);
}

CRT_TEST("central difference preserves input and performs two evaluations per parameter")
{
    ParameterVector parameters{1.0, 2.0, 3.0};
    const ParameterVector original = parameters;
    ParameterVector gradient(parameters.size());
    std::size_t evaluations = 0;
    const LossFunction loss = [&evaluations](ConstParameterView x) {
        ++evaluations;
        Scalar total = 0.0;
        for (const Scalar value : x) {
            total += value * value;
        }
        return total;
    };

    centralDifferenceGradient(loss, parameters, gradient);

    CRT_REQUIRE(parameters == original);
    CRT_REQUIRE(evaluations == 2 * parameters.size());
}

CRT_TEST("central difference accepts an empty parameter vector")
{
    const ParameterVector parameters;
    ParameterVector gradient;
    std::size_t evaluations = 0;

    centralDifferenceGradient(
        [&evaluations](ConstParameterView) {
            ++evaluations;
            return 0.0;
        },
        parameters,
        gradient);

    CRT_REQUIRE(gradient.empty());
    CRT_REQUIRE(evaluations == 0);
}

CRT_TEST("central difference validates dimensions and settings")
{
    const ParameterVector parameters{1.0};
    ParameterVector wrongSize(2);
    const LossFunction loss = [](ConstParameterView x) { return x[0]; };

    CRT_REQUIRE_THROWS(
        std::invalid_argument,
        centralDifferenceGradient(loss, parameters, wrongSize));

    ParameterVector gradient(1);
    CentralDifferenceSettings invalid;
    invalid.absoluteStep = 0.0;
    invalid.relativeStep = 0.0;
    CRT_REQUIRE_THROWS(
        std::invalid_argument,
        centralDifferenceGradient(loss, parameters, gradient, invalid));

    invalid.absoluteStep = std::numeric_limits<Scalar>::infinity();
    CRT_REQUIRE_THROWS(
        std::invalid_argument,
        centralDifferenceGradient(loss, parameters, gradient, invalid));
}
