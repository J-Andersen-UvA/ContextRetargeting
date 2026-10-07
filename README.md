# Context Retargeting

Portable C++ and Unreal Engine tooling for context-aware skeletal-animation retargeting, inspired by the [ReConForM strategy](https://arxiv.org/abs/2502.21207).

The project starts with an ordinarily retargeted target animation and optimizes configured target bone rotations so that important spatial relationships from the source animation are better preserved. Context points placed on both characters describe those relationships.

## Repository structure

The code is divided into three layers with a one-way dependency direction:

```text
Optimization
    <- ContextSolver
        <- UnrealContextOptimization
```

- [Optimization](Optimization/README.md) is the domain-independent numerical
  layer. It contains central numerical gradients, Adam, and the generic
  `minimizeWithAdam()` operation.
- [ContextSolver](ContextSolver/README.md) is the portable skeletal-animation
  layer. It interprets parameters as local bone rotations, performs forward
  kinematics, evaluates context descriptors and losses, and returns scalar loss
  values to `Optimization`.
- [UnrealContextOptimization](UnrealContextOptimization/README.md) is the
  Unreal Engine editor plugin. It provides point authoring, configuration
  assets, diagnostics, and animation baking.

The boundary between `Optimization` and `ContextSolver` is intentionally narrow: `Optimization` only sees numerical parameter vectors, gradients, and scalar losses. Skeletal-animation and Unreal concepts do not enter that layer.

## Portable build and tests

Building `ContextSolver` also builds its `Optimization` dependency:

```powershell
cmake -S ContextSolver -B ContextSolver/build
cmake --build ContextSolver/build --config Debug
ctest --test-dir ContextSolver/build -C Debug --output-on-failure
```

To build and test only the numerical layer:

```powershell
cmake -S Optimization -B Optimization/build
cmake --build Optimization/build --config Debug
ctest --test-dir Optimization/build -C Debug --output-on-failure
```

Both portable libraries use C++20 and have no third-party dependencies.

## Unreal Engine plugin

Copy `UnrealContextOptimization` into an Unreal project's `Plugins` directory, regenerate project files if needed, and build the editor target. The plugin currently targets Unreal Engine 5.7 and 5.8 APIs.

The plugin contains a vendored copy of the portable source so that its folder can be moved independently. The top-level `Optimization` and `ContextSolver` directories are the canonical portable implementation; changes to them must also be mirrored into the plugin's `ContextRetargetingCore` module.

For installation, configuration, baking, and diagnostic instructions, see the [Unreal plugin README](UnrealContextOptimization/README.md). For authoring the default context-point layout, see the [Context Point Placement Guide](UnrealContextOptimization/POINT_PLACEMENT_GUIDE.md).

## Current scope

The project currently supports editor-side animation baking with configurable bone rotation degrees of freedom, adaptive point relationships, distance, direction, penetration, height, point-position regularization, temporal jerk smoothing, and overlapping optimization windows.

Finger and hand-shape refinement is planned as a separate pass. Runtime retargeting and GPU optimization are not currently implemented.
