# Context Solver

`ContextSolver` is a portable skeletal-animation layer inspired by ReConForM.
It interprets numerical parameters as bone rotation changes, evaluates the
resulting poses and context points, and returns a scalar loss to the
`Optimization` library.


## Intended solve flow

```text
source pose batch
    -> evaluate source context points and relationships

initially retargeted target pose batch
    -> apply candidate local bone rotation changes
    -> forward kinematics
    -> evaluate target context points and relationships
    -> calculate total loss
    -> Optimization estimates gradients and updates the candidate
```

The initial target poses come from an ordinary retargeting pass. A zero-valued
candidate therefore represents the unchanged retargeted animation.

## Batched frames

Adam's moving averages do not create a batch of animation frames. They average
gradients over optimization iterations. If the loss function evaluates only
one frame, Adam still optimizes only that frame.

Following ReConForM, the Context Solver accepts a batch of frames and uses
one candidate containing the local bone rotation changes for every frame in
the batch. This allows losses to depend on both spatial relationships within a
frame and temporal relationships across frames.

The first implementation may use a small batch while validating correctness.
Batch size, overlap, latency, and runtime scheduling belong to the host adapter
and solver settings rather than the Optimization layer.

## Bone rotation degrees of freedom

The Context Solver describes adjustable local bone rotations with
`BoneRotationDegreeOfFreedom`. Each degree of freedom maps one scalar candidate
value to a local bone rotation in radians.

```cpp
struct BoneRotationDegreeOfFreedom
{
    BoneIndex bone;
    Vector3 localAxis;
};
```

There is no generic public parameterization concept. Context builds the
candidate vector from the configured bone rotation degrees of freedom and
passes only the numerical vector to Optimization.

## Point-position regularization

The total loss includes point-position regularization:

```text
regularizationLoss =
    sum over target points and frames(
        squaredDistance(
            candidatePointPosition,
            initialRetargetedPointPosition))
```

The total loss initially has the form:

```text
totalLoss =
    distanceLossWeight * distanceLoss
    + regularizationLossWeight * regularizationLoss
```

Distance relationships alone can have several equally valid skeletal
solutions. For example, a hand-to-chest distance may be matched by changing
the arm, shoulder, spine, root, or a combination of them. Point-position
regularization gives the objective an explicit preference for a solution near
the initially retargeted motion while still allowing the relationship loss to
correct contacts.

Limiting the number of Adam steps is not equivalent to regularization. The
number of steps controls how long the solver searches; regularization controls
which solution the solver prefers. Early stopping depends on learning rate,
loss scale, gradient scale, and optimizer state, so it does not reliably select
the solution closest to the initial retargeted motion.

Regularization is evaluated on target context-point positions, matching the
ReConForM formulation, rather than directly on the numerical bone rotation
parameters.

## Spatial descriptors

Each configured point relationship can preserve distance, direction, signed
penetration, or any combination of the three. Signed penetration projects the
offset from the first point to the second onto the first point's normal. The
normal is stored in the point's parent-bone space and follows the bone during
forward kinematics.

Height compares each point's ground-relative coordinate along the configured up
axis. It also penalizes target points below the target ground height.

## Adaptive per-frame weights

Source-driven adaptive weights follow ReConForM's proximity rule. A relationship
receives full weight below the minimum interaction distance, fades to zero at the
maximum distance, and stays inactive while its source points are farther apart.
Height matching behaves the same way near the source ground. Thresholds are
fractions of source character height and default to 5% and 15%.

Adaptive weights can be disabled globally or for an individual point
relationship. Target weights are recalculated from the current candidate between
Adam iterations and remain constant during each central-difference calculation.
This reproduces stop-gradient behavior with numerical differentiation.

The distance component is:

```text
distanceLoss =
    sum over active relationships and frames(
        weight
        * squared(
            sourcePointDistance - targetPointDistance))
```

Direction uses the squared cosine distance
`squared(1 - cosineSimilarity)` between source and target offsets.

## Temporal smoothing

The point-jerk component uses the magnitude of the third finite difference of
candidate target point positions over four consecutive frames, divided by the
cube of the batch frame interval:

```text
(point[t+3] - 3 * point[t+2] + 3 * point[t+1] - point[t])
    / secondsPerFrame^3
```

Its magnitude is measured in metres per second cubed and penalizes sudden changes
in point acceleration. This keeps the loss meaningful across different animation
sample rates. The host decides whether to solve the complete animation or
overlapping windows; the portable Context Solver only evaluates the batch it
receives.

Central-difference evaluation also returns weighted L2 gradient magnitudes for
each loss component and for their combined gradient. These values show which
losses are actually driving parameter updates; scalar loss values alone do not
provide that information.

The Context Solver uses frame-local central differences. It evaluates the full
candidate geometry once per Adam iteration. A parameter perturbation then
rebuilds only that parameter's frame, its spatial loss, and the point-jerk
stencils containing that frame. The generic full-loss central-difference
implementation remains available in `Optimization` and is used as the parity
reference in tests.

Points placed exactly at joint origins do not move under rotation around the
adjacent limb axis. Point jerk therefore cannot observe all forearm twist when
the only samples are the elbow and wrist origins. Add a forearm point with a
bone-local offset toward the skin surface so axial forearm rotation moves that
point. The same principle applies to non-collinear palm points for observing
hand orientation.

## Unreal Engine integration

For Unreal Engine 5.7 and 5.8, the intended adapter is a custom IK Retargeter
operation placed after the operations that create the initial target pose. The
adapter reads the source pose and current target output pose, converts them to
portable Context Solver data, and writes the corrected target pose back.

The IK Retargeter operation runs on one pose at a time. A batch
therefore requires one of these host strategies:

- Offline baking: collect an animation range, solve the batch, and write the
  corrected animation.
- Buffered runtime solving: keep a rolling window and accept output latency.
- Causal runtime solving: use the current and previous frames without future
  frames, which reduces latency but differs from full-window optimization.

The portable Context Solver receives a frame batch regardless of which host
strategy produced it. Unreal-specific buffering, animation assets, threading,
and editor UI remain in the Unreal adapter.

## Current scope

The current implementation includes:

- Unreal-compatible quaternion and rigid-transform composition order.
- Parent-before-child transform hierarchies and forward kinematics.
- Separate source and target character definitions.
- Batched source and initially retargeted target poses.
- Bone-attached context points.
- Distance, direction, signed-penetration, and height descriptors.
- Source-driven adaptive per-frame relationship and floor weights.
- Local bone rotation degrees of freedom measured in radians.
- Point-position regularization against the initially retargeted target poses.
- Point-jerk temporal smoothing for batches containing at least four frames.
- Central numerical gradients and Adam through the `Optimization` library.

It does not yet include skinned context points, target-dependent adaptive
weights, root translation, or sliding.

## Build and test

From this directory:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The library target is:

```cmake
ContextRetargeting::ContextSolver
```

