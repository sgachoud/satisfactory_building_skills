# Transform

Press **T** in Edit to enter Transform with the selected buildings. If nothing is
pinned, the hovered selection is used. Selection stays fixed until you leave this
state. **Esc** returns to normal Edit with the selection and applied changes intact;
it is not an undo command.

## Controls

- The existing configured Build Gun mode key (normally **R**) cycles **Rotate X,
  Rotate Y, Rotate Z, Scale XYZ, Scale XY, Scale XZ, Scale YZ, Scale X, Scale Y,
  Scale Z**, then wraps to Rotate X.
- **Scroll up/down** applies/reverses one step of the active operation.
- **Up/Down/Left/Right** move along the selected frame's X/Y axes nearest the camera's
  forward/right directions. Local is the default; a tilted frame can introduce world-Z movement.
- **Page Up/Page Down** move along the selected frame's Z axis.
- **K** opens the temporary step settings window. Type movement distances in cm,
  rotation angles in degrees and scale multipliers for Normal, Tiny, Small and Large.
  Enter or leaving a field applies its value. Close or Esc returns to Transform.
  Values survive returning to normal Edit and reopening Transform; closing Edit
  restores the mod configuration. This window never writes those configured values.
- **P** cycles Bounds center, Reference, Individual and Origins center pivots.
- **L** cycles World / Local axes, plus Anchor axes when an Alt+A anchor exists.
  The choice applies to movement, rotation and scaling.
- **B** cycles the reference building. The building aimed at when entering Transform is
  the initial reference if it belongs to the selection. This is separate from Alt+A
  which chooses the external alignment anchor.
- **Ctrl+Z / Ctrl+Y** undo/redo the last transform in Edit or Transform. These
  configurable bindings are shared with Plan, Build and Dismantle.
- Three held modifiers choose Tiny, Small or Large steps. Tiny takes precedence,
  then Small, then Large if multiple modifiers are held.

T, K, all six movement commands, and the step modifiers are in the game's keybinding
options under Building Skills. R continues to use the existing Build Gun binding.
Step sizes and the hold-repeat delay are in the Building Skills mod configuration.
Normal Edit controls return after Esc; grouping and multi-selection do not operate
inside Transform.

| Step | Default held key | Movement | Rotation | Scale multiplier |
| --- | --- | --- | --- | --- |
| Normal | None | 1 m | 15 degrees | 1.1 |
| Tiny | Left Ctrl | 0.2 m | 1 degree | 1.01 |
| Small | Left Alt | 0.5 m | 5 degrees | 2 |
| Large | Left Shift | 2.5 m | 45 degrees | 11 |

Movement settings use centimetres. Scaling uses a multiplier directly: downward
scroll applies its reciprocal. This makes an up/down pair reversible. Movement
repeats every 0.15 seconds by default. Compatible queued steps are combined on slow
connections, within the per-operation bounds. Opposite queued movement and inverse
queued scroll steps cancel before being sent, so they cannot turn into a zero-scale
request or create an unnecessary undo entry. Already submitted edits still settle
normally. The left panel displays the currently effective amounts.

## Pivot and axes

Bounds center is the default pivot. Origins center averages native building origins;
Reference uses the selected reference building's origin; Individual keeps every building
at its own origin during rotation/scaling. Nested groups expand to individual members
with duplicates removed. Local axes follow the reference for a shared pivot and each
building's rotation for Individual. The axis display follows the active frame. Shared center pivots remain fixed during
rotation/scaling and follow translation, so inverse steps do not drift as bounds rotate.

Scale XYZ uniformly changes size and distance from the pivot without changing
rotation. Nonuniform scaling uses the chosen frame for member positions and projects
its logarithmic stretch onto each building's local axes. A quarter-turned building
therefore scales along the correct local axis. Oblique buildings use a shear-free
approximation: arbitrary world-axis stretching cannot be represented exactly by a
native rotation/scale transform. Reciprocal steps reverse this approximation, and
individual local scaling is exact.

Repeated compatible inputs are combined before submission. Cyan wire boxes preview
pending movement without changing collision or factory simulation. The server still
validates and applies every committed operation; the preview is not placement approval.
Groups unaffected by an operation keep their cached pivot. Connection neighbours and
actor lookups are indexed once per combined operation, without reusing stale topology
across separate commits.

Undo/redo captures all affected native transforms and spline/lift/wire geometry,
including unselected connections stretched by the edit. A continuous input gesture is
coalesced where its affected set remains unchanged. History is session-only, limited
to 32 operations per player and 64 MiB across players. It does not reverse factory
production, inventory changes, grouping operations or dismantling. Changed connections,
removed buildings or conflicting edits prevent that history operation from applying.

## Native data and current limits

Transforms operate on existing actors and lightweight instance records. They keep
inventories, recipe/customization data, runtime lightweight indices and blueprint
membership. Native instanced visuals, collision, factory-proximity registration and spatial lookup data are moved
with the saved building transforms. Group references and pivots are updated in the
same operation; no permanent parent actor is created.

Connected machines, conveyor belts/lifts, pipes, rail tracks, wires, train platforms,
extractors, and lightweight structures use their existing native objects. Connections
inside the selection remain connected. An unselected belt, pipe or rail adjoining a
moved endpoint stretches while its far endpoint remains fixed. Power-wire endpoints
follow their attached buildings. A selected spline attached directly to an unselected
rigid building keeps that attachment anchored.

Spline updates rebuild saved control points, visual instances, collision, clearance,
and endpoint positions. Old body-setup boxes are cleared before re-registering spline
collision, preventing stale or duplicate shapes after repeated edits. Conveyor chains are retired through the native subsystem and
rebuilt with their items retained. Pipe contents remain intact while elevation and
network caches refresh. Train-platform tracks and spline attachments follow their
owners. Extractors retain the original resource node; moving one does not claim a new
node or move the map's resource deposit. Every affected building is validated before
mutation, and an unexpected native setter failure rolls back the batch.

The server validates building identity, selection size, selected building fingerprints, player state,
range, finite values and scale/world bounds. Steps are limited to 100 m translation,
180 degrees per axis from controls, and scale components between 0.01 and 100.
Transforms deliberately allow clipping; they do not run hologram placement rules.

Ordinary actors initially arrive through Unreal's quantized spawn transform. The
first edit accepts that native representation as well as the exact server transform.
Position and scale use the engine's packed-vector quantization helper, including
negative midpoint rounding; they are not rounded with a separate approximation.
After an edit, full-precision transforms and per-building versions reject stale
requests, including changes smaller than the initial network rounding. These
versions are transient and add no persistent save data.

Late-join geometry uses bounded reliable batches with per-building sequence numbers
to prevent an older snapshot overwriting a newer edit. Only one snapshot chunk is in
flight per client. A superseded or delayed transform reply releases the local selection
after ten seconds without reverting world changes. Delayed native objects are
retried and their network caches refreshed after successful application.

Multiplayer replication and actual save/reload still require dedicated gameplay
coverage. Native transforms are the intended source of persistence; this does not
claim every building type or mod-removal scenario has been verified.

Position updates do not increment the group topology revision. Receiving clients
apply changed member positions and ancestor pivots from the transform batches,
including replay over a topology snapshot that started before the move. Per-building
versions still reject stale edits. Local outline proxies move alongside their
buildings without changing selection colors. This removes the full group-tree
transfer from each nudge; ordinary network acknowledgement still applies.

## Selected pose

For one object or one complete placed blueprint, the left panel shows position in
centimetres, rotation in degrees (X roll, Y pitch, Z yaw), and scale multipliers.
These are world values unless an Alt+A anchor exists, in which case they are
relative to that anchor. This readout does not depend on the chosen movement axes.
Blueprint origins follow complete-blueprint transforms and transform undo/redo.

## Repeating transforms

Active [transformers](TRANSFORMERS.md) repeat translation, rotation and scaling on
existing matching counterparts. Movement is relative to each transformer's frame:
a mirror reflects the movement and a rotation transformer rotates it. Counterparts
and their connected geometry join the original action's undo/redo history.
