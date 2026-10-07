# Context Retargeting Optimization

`Optimization` is the lowest, domain-independent layer of the context
retargeting project. It works only with numerical parameter vectors, scalar
loss functions, gradients, and parameter updates.

The intended boundary is:

```text
Optimization candidate x
        -> Context evaluates loss(x)
        -> Optimization receives scalar loss
        -> Optimization estimates a gradient and updates x
```

The library uses C++20 and has no third-party dependencies.

## Components

- `types.hpp` defines scalar, vector, view, loss-function, and
  gradient-function types.
- `numerical_gradient.hpp` provides central finite-difference gradients.
- `adam.hpp` provides the independently usable Adam parameter-update step.
- `minimize.hpp` provides `minimizeWithAdam()`, which coordinates a loss
  function, an injected gradient strategy, and Adam.

Numerical differentiation and Adam are independent. A future analytic or
automatic differentiation strategy can implement `GradientFunction` without
changing Adam.

## Minimal example

```cpp
#include "context_retargeting/optimization/minimize.hpp"
#include "context_retargeting/optimization/numerical_gradient.hpp"

using namespace context_retargeting::optimization;

const LossFunction loss = [](ConstParameterView x) {
    const Scalar error = x[0] - 3.0;
    return error * error;
};

const GradientFunction gradient = makeCentralDifferenceGradient(loss);

AdamSettings adamSettings;
adamSettings.learningRate = 0.1;

OptimizationSettings optimizationSettings;
optimizationSettings.maxIterations = 200;

const OptimizationResult result = minimizeWithAdam(
    ParameterVector{0.0},
    loss,
    gradient,
    adamSettings,
    optimizationSettings);
```

## Build and test

From this directory:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The library target is:

```cmake
ContextRetargeting::Optimization
```

Tests are enabled by default. Disable them with:

```powershell
cmake -S . -B build -DCONTEXT_RETARGETING_OPTIMIZATION_BUILD_TESTS=OFF
```

## Public API behavior

- Central differences evaluate the loss twice per parameter and do not modify
  the caller's parameter vector.
- Adam receives an already-computed gradient and updates parameters in place.
- `minimizeWithAdam()` reports the final parameters, their corresponding loss,
  the number of Adam updates, the loss after every update, and the stopping
  reason.
- The returned parameters and loss are the best finite result encountered, not
  necessarily the final Adam update before stopping.
- Relative-loss early stopping compares the current loss with the loss from the
  configured number of previous updates. Set the minimum improvement to zero to
  disable it.
- Invalid configuration and dimension mismatches throw `std::invalid_argument`.
- Non-finite loss or gradient values produce an explicit stopping reason.

