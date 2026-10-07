# Unreal Context Optimization

Editor-side animation baking for the portable `Optimization` and `ContextSolver`
libraries. The plugin duplicates an initial target animation, optimizes target bone
rotations from source/target context-point relationships, and writes the solved
rotations into the duplicate.

Unreal positions, context-point offsets, character heights, and ground heights
are converted from centimeters to meters before they enter the portable solver.
Solved translations are converted back to centimeters when animation tracks are
written. Rotations, normals, and axis directions are not scaled.

The plugin targets Unreal Engine 5.7 and 5.8 APIs. It has no external library or
package dependency. A copy of the portable C++ source is included in the plugin so
the plugin can be moved into an Unreal project by itself.

## Modules

- `ContextRetargetingCore` builds the vendored portable C++ solver with C++20 and
  exceptions enabled.
- `UnrealContextOptimization` contains the configuration asset, sphere point
  component, and point authoring actor. This module does not modify assets.
- `UnrealContextOptimizationEditor` contains the editor Blueprint validation,
  cost estimate, and bake operations.

## Configuration asset

Create a Data Asset whose class is `ContextRetargetConfiguration`. It contains:

- the source and target skeletal meshes;
- paired source and target context points;
- distance relationships between named context points;
- target bone rotation degrees of freedom;
- loss, numerical-gradient, Adam, and iteration settings.

Each context point is stored relative to one bone. A point pair uses one shared
`pointName`, with separate source and target bone attachments. Relationships use
the point names, so array ordering is not used to identify them.

A target bone rotation entry has `X`, `Y`, and `Z` checkboxes for its local rotation
axes. Enable all three when that bone may rotate freely, or only the axes that are
needed. The Unreal layer expands the checked axes into individual solver parameters.
Parameters are rotation angles in radians. Do not add bones that should remain
unchanged.

Point-position regularization is applied to every target context point on every
frame. It keeps the solution near the initial retargeted animation while the
distance loss improves the desired relationships. Reducing the iteration count is
not equivalent: it only stops Adam early and does not define which otherwise valid
solution should be preferred.

The `Configuration Tools` section provides two editor operations:

- Set `pointNameToRemove` and press `Remove Point And Relationships` to remove a
  captured point pair and every relationship that references it.
- Press `Export Configuration JSON` to write a readable copy to
  `Saved/ContextOptimization/<asset name>.json` in the Unreal project.

## Point authoring

See [Context Point Placement Guide](POINT_PLACEMENT_GUIDE.md) for the placement
of every point included in the default preset.

Create a Blueprint derived from `ContextPointAuthoringActor` for each character:

1. Open the source Actor Blueprint and set `character` to `Source`.
2. Assign its skeletal mesh and the configuration asset.
3. Open `Import Points From Preset` and select `reconformUpperBody`, or browse
   to another preset JSON file.
4. For each additional point, set `newPointName` and `newPointBone`, then press
   `Add Context Point`. The button adds a saved component to the Blueprint's
   component hierarchy.
5. Move every sphere from its bone origin to the required mesh surface position.
6. Press `Capture Points To Configuration`.
7. Repeat this in the target Actor Blueprint with `character` set to `Target` and
   the same point names.

Point presets are JSON files in `Resources/ContextPointPresets`. The import menu
lists every JSON file in that folder and also provides a file browser. Each point
has a `pointName` and a CC-style `referenceBone`.
The loader first tries the bone name directly, then a matching bone-name suffix,
then Unreal's IK Rig auto-characterization and standard retarget chains. It logs
the matched skeleton template and lists every unresolved point.

The included `reconformUpperBody` preset is an upper-body approximation inspired
by ReConForM's manually selected surface points. ReConForM describes 41 points on
the SMPL template but does not publish a named point-to-bone table. The preset
therefore supplies editable head, torso, pelvis, arm, and hand points and excludes
legs and feet. All generated points start at their bone origins; their surface
positions must be authored for each mesh. Existing point names are left unchanged,
so the button can safely be used again after the preset JSON is extended.

After capturing the source and target points, open the configuration asset and
use `Import Relationships From Preset`. Importing replaces the existing
relationship array and can be undone. The default preset uses
`allOrderedPairs`, matching ReConForM's directed pairwise descriptor matrices.
The preset sets `excludeSameTargetBone` so relationships between two points
attached to the same target bone are omitted. Its 32 points expand to 918
relationships for the default bone assignments, including penetration in both
directions. Import relationships after capturing the target points so the importer
can compare their resolved target bones. Adaptive weighting determines which
relationships are active for a given source frame.

The `upperBodyNoCloseRelationships` preset generates the same relationships but
sets `useDirection` to false in both directions for chest-to-upper-arm,
upper-arm-to-elbow, and forearm-to-wrist pairs. Distance and penetration remain
enabled. Generated rules declare these undirected exceptions in
`directionDisabledPairs`.

Relationship entries may instead provide explicit `firstPoint` and
`secondPoint` names. `allUniquePairs` is also supported for an unordered set.

The sphere components attach to their selected bones. Assign `previewAnimation`
and press `Play Preview Animation` to check the points while the animation plays.
Press `Stop Preview Animation` to stop it. If a component's `boneName` is changed,
press `Refresh Point Attachments`.

Capturing updates one side of each pair and assigns that actor's skeletal mesh to
the configuration. It does not delete pairs authored by the other actor.
The captured `localPosition` is the sphere component's Location relative to its
bone. Context points in the configuration are generated data and are read-only in
the asset editor.

When adding a `ContextPointComponent` manually, make it a child of the Skeletal
Mesh component and set `Parent Socket` to the same bone as `boneName`. Then reset
Location before positioning the sphere. `Refresh Point Attachments` performs this
attachment for all point components in the Actor Blueprint.

The same buttons also work on an actor placed in a level. In that case, added
points belong only to that placed actor instance rather than to the Actor Blueprint.

## Relationships and degrees of freedom

Add point relationships directly on the configuration asset. The C++ property is
still named `distanceRelationships` so existing assets remain compatible, but the
Unreal details panel displays it as `Point Relationships`. For example,
with points named `leftHand`, `rightHand`, and `head`, useful relationships can be
`leftHand-rightHand`, `leftHand-head`, and `rightHand-head`.

Each relationship has checkboxes for distance, direction, signed penetration,
and adaptive weighting. Penetration uses the first point's normal, so relationship
order matters for that descriptor. Rotate a context-point sphere so its local X
axis points outward from the surface, then capture the points again. The sphere is
symmetrical, but its rotation gizmo shows the local X axis.

For temporal arm smoothing, do not use only points at the elbow and wrist bone
origins. Add another point parented to the forearm with a local-position offset
toward the skin surface. Rotation around the forearm axis then moves this point,
allowing point-jerk smoothing to observe forearm twist. Non-collinear points on
the palm serve the same purpose for hand orientation.

Height is evaluated for every context point. `worldUp`, `sourceGroundHeight`, and
`targetGroundHeight` define the two ground planes. The defaults match Unreal's Z-up
coordinate system and a ground plane at Z=0.

Adaptive weights use the source character's bounds height. Relationships closer
than 5% of character height receive full weight and fade to zero at 15%. Disable
`Use Adaptive Weight` for a structural relationship that should remain active even
when its points are far apart. Character height is read from the skeletal mesh's
imported bounds at bake time. It is written to the Output Log and JSON export.

Solver settings are displayed in collapsible Loss, Smoothing, Adaptive Weights,
Environment, Numerical Gradient, Adam, and Iterations groups. Temporal smoothing
adds the magnitude of physical point jerk, measured in metres per second cubed,
over scheduled frame windows. The default window duration is three seconds.
Neighboring windows overlap by 0.25 seconds, and their parameter corrections are
smoothly blended through the overlap.

Older plugin versions used an unscaled third finite difference. Jerk is now
divided by `secondsPerFrame^3`. At 24 FPS, `0.00000217014` gives approximately the
same effective strength as the former `0.03` setting. Existing configuration
assets are not modified automatically.

The Iterations group also controls relative-loss early stopping. The default
requires at least `0.0001` relative improvement over 10 iterations. Zero disables
this stopping rule. The Output Log reports loss every 10 iterations by default,
the final iteration, and the stopping reason for each window.

When `Use Target Adaptive Weights` is enabled, interaction weights begin with the
source proximity weights and progressively add proximity weights measured on the
current target candidate over the Adam iterations. This activates target-only
close relationships that can reveal false-positive contacts. Target weights are
held fixed across each central-difference calculation and recalculated for the
next Adam iteration.

Add `boneRotationDegreesOfFreedom` for target bones that can affect those points.
The solver does not choose bones randomly. Numerical gradients measure how every
configured degree of freedom changes the loss, and Adam updates all
configured parameters from those gradients.

## Editor Blueprint operations

`ContextOptimizationEditorLibrary` exposes:

- `Validate Context Bake`
- `Estimate Context Bake`
- `Bake Context Optimized Animation`

Use them from an Editor Utility Widget, Editor Utility Blueprint, or another
editor-only Blueprint. The bake inputs are:

- the source animation;
- the initial target animation produced by the normal retargeting pass;
- the configuration asset;
- an output package path such as `/Game/ContextBakes`;
- a new output asset name.

The source animation must use the source mesh's skeleton. The initial target
animation must use the target mesh's skeleton. Both animations currently need the
same sampled key count and sample frame rate. The output path must not already
contain an asset with the requested name.

The bake returns `success`, the new animation, an error message, the initial and
final weighted loss, and the largest number of Adam iterations used for one window.
After optimization finishes, the initial target animation is duplicated so its
curves, notifies, metadata, and scale keys are retained. The plugin replaces local
translation and rotation tracks; translation remains equal to the initial target
animation and rotation contains the optimized result.

## Cost

Without temporal smoothing, the editor bake solves one frame at a time. With
temporal smoothing, each scheduled window is solved once. Windows cover roughly
three seconds by default. Neighboring windows overlap and use a smooth blend of
their scalar rotation-parameter corrections in the overlap. The progress dialog
advances and can be cancelled between solves.

With central differences and `d` enabled target bone rotation degrees of freedom,
one Adam iteration for one frame uses:

`2 * d + 1` loss evaluations

For temporal smoothing with `w` frames in a window, one window iteration
uses:

`2 * w * d + 1` loss evaluations

The two evaluations for a perturbed parameter rebuild only its frame and the
point-jerk stencils containing that frame. They do not rebuild all `w` frames.

Across `n` scheduled windows, one iteration uses approximately:

`n * (2 * w * d + 1)` loss evaluations

Use `w = 1` when smoothing is disabled.

Use `Estimate Context Bake` before a large bake. Start with a short animation, a
small set of target degrees of freedom, and 10 to 20 iterations. Smoothing is
substantially more expensive than the independent-frame path.

## Animation diagnostics

The editor module contains the `ContextOptimizationDiagnostics` commandlet for
comparing a source animation, its initial retarget, and an optimized result over
a selected frame range. It writes:

- `frames.csv`: source-active, target-only, and combined-active relationship
  counts plus loss components per frame;
- `relationships.csv`: every relationship's source, initial-target,
  optimized-target, and combined adaptive weights plus individual loss components
  per frame;
- `relationship_summary.csv`: relationships ranked by accumulated optimized
  distance loss;
- `bones.csv`: sampled target bone transforms;
- `points.csv`: sampled source, initial-target, and optimized context points;
- `jacobian.csv`: the measured influence of each configured degree of freedom.

Point velocity, acceleration, and jerk columns use seconds and metres, so their
units are m/s, m/s^2, and m/s^3. A completed bake also logs raw loss components,
weighted loss contributions, and weighted L2 gradient magnitudes for every loss
component at the initial and final parameters.

Run it through `UnrealEditor-Cmd.exe` with `-run=ContextOptimizationDiagnostics`
and provide `-Source`, `-InitialTarget`, `-Optimized`, `-Configuration`,
`-StartFrame`, `-EndFrame`, and `-Output`. Asset arguments use Unreal object
paths such as `/Game/Folder/Asset.Asset`.

Add `-GradientAudit` to evaluate the optimized animation's reconstructed
parameter vector without running Adam. The audit calculates central-difference
gradients with absolute and relative steps of `0.001`, `0.0001`, and `0.00001`.
It adds:

- `gradient_step_summary.csv`: gradient norms, cosine agreement, and relative
  difference for every weighted loss and the total gradient;
- `gradient_parameter_stability.csv`: every parameter identified by frame,
  bone, and axis, including its three gradients and an instability flag;
- `gradient_loss_alignment.csv`: pairwise cosine agreement between loss
  gradients at step `0.0001`.

## Current boundaries

- Baking is synchronous and editor-only.
- The current implementation uses distance, direction, signed penetration,
  height, and point-position regularization.
- Adaptive interaction weights combine source and target proximity. Target
  weights use stop-gradient behavior during numerical differentiation.
- Point-jerk smoothing uses scheduled overlapping windows. Sliding is not implemented yet.
- Animation resampling is not performed.
- The portable solver ignores scale during forward kinematics. Original target
  scale keys are preserved in the output.
- The output package is marked dirty but is not automatically saved to disk.
