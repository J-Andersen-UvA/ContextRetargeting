#include "test_support.hpp"

#include "context_retargeting/optimization/adam.hpp"

#include <stdexcept>

using namespace context_retargeting::optimization;

CRT_TEST("Adam performs a bias-corrected first step")
{
    AdamSettings settings;
    settings.learningRate = 0.1;
    settings.epsilon = 1.0e-8;
    Adam adam(1, settings);
    ParameterVector parameters{1.0};
    const ParameterVector gradient{0.5};

    adam.step(parameters, gradient);

    const Scalar expected = 1.0 - 0.1 * 0.5 / (0.5 + settings.epsilon);
    CRT_REQUIRE_NEAR(parameters[0], expected, 1.0e-14);
    CRT_REQUIRE(adam.iteration() == 1);
}

CRT_TEST("Adam reset reproduces a fresh optimizer")
{
    Adam adam(2);
    const ParameterVector gradient{0.25, -0.75};
    ParameterVector first{2.0, -1.0};
    adam.step(first, gradient);
    adam.step(first, gradient);

    adam.reset();
    ParameterVector second{2.0, -1.0};
    adam.step(second, gradient);
    adam.step(second, gradient);

    CRT_REQUIRE(first == second);
    CRT_REQUIRE(adam.iteration() == 2);
    CRT_REQUIRE(adam.parameterCount() == 2);
}

CRT_TEST("Adam leaves parameters unchanged for an initial zero gradient")
{
    Adam adam(2);
    ParameterVector parameters{2.0, -1.0};
    const ParameterVector gradient{0.0, 0.0};

    adam.step(parameters, gradient);

    CRT_REQUIRE(parameters[0] == 2.0);
    CRT_REQUIRE(parameters[1] == -1.0);
}

CRT_TEST("Adam validates dimensions")
{
    Adam adam(2);
    ParameterVector parameters{1.0};
    const ParameterVector gradient{1.0};

    CRT_REQUIRE_THROWS(
        std::invalid_argument,
        adam.step(parameters, gradient));
}

CRT_TEST("Adam validates settings")
{
    AdamSettings settings;
    settings.learningRate = 0.0;
    CRT_REQUIRE_THROWS(std::invalid_argument, Adam(1, settings));

    settings = {};
    settings.beta1 = 1.0;
    CRT_REQUIRE_THROWS(std::invalid_argument, Adam(1, settings));

    settings = {};
    settings.beta2 = -0.1;
    CRT_REQUIRE_THROWS(std::invalid_argument, Adam(1, settings));

    settings = {};
    settings.epsilon = 0.0;
    CRT_REQUIRE_THROWS(std::invalid_argument, Adam(1, settings));
}
