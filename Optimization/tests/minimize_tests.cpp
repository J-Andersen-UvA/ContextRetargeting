#include "test_support.hpp"

#include "context_retargeting/optimization/minimize.hpp"
#include "context_retargeting/optimization/numerical_gradient.hpp"

#include <limits>

using namespace context_retargeting::optimization;

namespace {

LossFunction shiftedQuadraticLoss()
{
    return [](ConstParameterView x) {
        const Scalar dx = x[0] - 3.0;
        const Scalar dy = x[1] + 2.0;
        return dx * dx + dy * dy;
    };
}

GradientFunction shiftedQuadraticGradient()
{
    return [](ConstParameterView x, ParameterView gradient) {
        gradient[0] = 2.0 * (x[0] - 3.0);
        gradient[1] = 2.0 * (x[1] + 2.0);
    };
}

} // namespace

CRT_TEST("minimize converges with an analytic gradient strategy")
{
    AdamSettings adam;
    adam.learningRate = 0.1;
    OptimizationSettings optimization;
    optimization.maxIterations = 300;

    const auto result = minimizeWithAdam(
        {10.0, -8.0},
        shiftedQuadraticLoss(),
        shiftedQuadraticGradient(),
        adam,
        optimization);

    CRT_REQUIRE(result.stopReason == StopReason::MaxIterations);
    CRT_REQUIRE(result.iterations == 300);
    CRT_REQUIRE_NEAR(result.parameters[0], 3.0, 1.0e-5);
    CRT_REQUIRE_NEAR(result.parameters[1], -2.0, 1.0e-5);
    CRT_REQUIRE_NEAR(result.loss, 0.0, 1.0e-9);
}

CRT_TEST("minimize accepts a numerical gradient strategy")
{
    const LossFunction loss = shiftedQuadraticLoss();
    const GradientFunction gradient = makeCentralDifferenceGradient(loss);
    AdamSettings adam;
    adam.learningRate = 0.1;
    OptimizationSettings optimization;
    optimization.maxIterations = 300;

    const auto result = minimizeWithAdam(
        {10.0, -8.0}, loss, gradient, adam, optimization);

    CRT_REQUIRE_NEAR(result.parameters[0], 3.0, 1.0e-5);
    CRT_REQUIRE_NEAR(result.parameters[1], -2.0, 1.0e-5);
    CRT_REQUIRE_NEAR(result.loss, 0.0, 1.0e-9);
}

CRT_TEST("minimize stops before an update when gradient tolerance is met")
{
    OptimizationSettings settings;
    settings.maxIterations = 20;
    settings.gradientTolerance = 1.0e-6;

    const auto result = minimizeWithAdam(
        {3.0, -2.0},
        shiftedQuadraticLoss(),
        shiftedQuadraticGradient(),
        {},
        settings);

    CRT_REQUIRE(result.stopReason == StopReason::GradientTolerance);
    CRT_REQUIRE(result.iterations == 0);
    CRT_REQUIRE(result.loss == 0.0);
}

CRT_TEST("minimize treats max iterations as the number of Adam updates")
{
    OptimizationSettings settings;
    settings.maxIterations = 0;
    std::size_t gradientEvaluations = 0;

    const auto result = minimizeWithAdam(
        {5.0},
        [](ConstParameterView x) { return x[0] * x[0]; },
        [&gradientEvaluations](ConstParameterView x, ParameterView gradient) {
            ++gradientEvaluations;
            gradient[0] = 2.0 * x[0];
        },
        {},
        settings);

    CRT_REQUIRE(result.iterations == 0);
    CRT_REQUIRE(result.parameters[0] == 5.0);
    CRT_REQUIRE(result.loss == 25.0);
    CRT_REQUIRE(gradientEvaluations == 0);
}

CRT_TEST("minimize records loss history and stops after insufficient improvement")
{
    OptimizationSettings settings;
    settings.maxIterations = 50;
    settings.minimumRelativeLossImprovement = 1.0e-6;
    settings.lossImprovementPatience = 4;
    std::size_t lastReportedIteration = 0;
    std::size_t progressCallCount = 0;
    settings.progress = [&lastReportedIteration, &progressCallCount](
                            std::size_t iteration,
                            Scalar loss) {
        lastReportedIteration = iteration;
        ++progressCallCount;
        CRT_REQUIRE(loss == 5.0);
    };

    const auto result = minimizeWithAdam(
        {1.0},
        [](ConstParameterView) { return 5.0; },
        [](ConstParameterView, ParameterView gradient) { gradient[0] = 0.0; },
        {},
        settings);

    CRT_REQUIRE(result.stopReason == StopReason::LossImprovement);
    CRT_REQUIRE(result.iterations == 4);
    CRT_REQUIRE(result.lossHistory.size() == 5);
    CRT_REQUIRE(lastReportedIteration == 4);
    CRT_REQUIRE(progressCallCount == 5);
}

CRT_TEST("minimize returns the best parameters after loss increases")
{
    OptimizationSettings settings;
    settings.maxIterations = 10;
    settings.minimumRelativeLossImprovement = 1.0e-6;
    settings.lossImprovementPatience = 1;
    AdamSettings adam;
    adam.learningRate = 0.1;

    const auto result = minimizeWithAdam(
        {0.0},
        [](ConstParameterView x) { return x[0] * x[0]; },
        [](ConstParameterView, ParameterView gradient) { gradient[0] = -1.0; },
        adam,
        settings);

    CRT_REQUIRE(result.stopReason == StopReason::LossImprovement);
    CRT_REQUIRE(result.iterations == 1);
    CRT_REQUIRE(result.lossHistory.back() > 0.0);
    CRT_REQUIRE(result.parameters[0] == 0.0);
    CRT_REQUIRE(result.loss == 0.0);
}

CRT_TEST("minimize can return the final parameters for a scheduled objective")
{
    OptimizationSettings settings;
    settings.maxIterations = 1;
    settings.returnBestParameters = false;
    AdamSettings adam;
    adam.learningRate = 0.1;

    const auto result = minimizeWithAdam(
        {0.0},
        [](ConstParameterView x) { return x[0] * x[0]; },
        [](ConstParameterView, ParameterView gradient) { gradient[0] = -1.0; },
        adam,
        settings);

    CRT_REQUIRE(result.parameters[0] > 0.0);
    CRT_REQUIRE(result.loss > 0.0);
}

CRT_TEST("minimize reports non-finite loss and gradient")
{
    const Scalar nan = std::numeric_limits<Scalar>::quiet_NaN();

    const auto lossFailure = minimizeWithAdam(
        {1.0},
        [nan](ConstParameterView) { return nan; },
        [](ConstParameterView, ParameterView gradient) { gradient[0] = 0.0; });
    CRT_REQUIRE(lossFailure.stopReason == StopReason::NonFiniteLoss);
    CRT_REQUIRE(lossFailure.iterations == 0);

    const auto gradientFailure = minimizeWithAdam(
        {1.0},
        [](ConstParameterView x) { return x[0] * x[0]; },
        [nan](ConstParameterView, ParameterView gradient) { gradient[0] = nan; });
    CRT_REQUIRE(gradientFailure.stopReason == StopReason::NonFiniteGradient);
    CRT_REQUIRE(gradientFailure.iterations == 0);
}
