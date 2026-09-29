# Transform

Press **T** in Edit to enter Transform with the selected buildings. If nothing is
pinned, the hovered selection is used. Selection stays fixed until you leave this
state. **Esc** returns to normal Edit with the selection and applied changes intact;
it is not an undo command.

## Controls

- The existing configured Build Gun mode key (normally **R**) cycles **Rotate X,
  Rotate Y, Rotate Z, Rotate XYZ, Scale XY, Scale XZ, Scale YZ, Scale X, Scale Y,
  Scale Z**, then wraps to Rotate X.
- **Scroll up/down** applies/reverses one step of the active operation.
- **Up/Down/Left/Right** move along the horizontal world axes nearest the camera's
  forward/right directions. Looking up or down does not introduce vertical movement.
- **Page Up/Page Down** move along world Z.
- Three held modifiers choose Tiny, Small or Large steps. Tiny takes precedence,
  then Small, then Large if multiple modifiers are held.

T, all six movement commands, and the step modifiers are in the game's keybinding
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
repeats every 0.15 seconds by default, with no backlog of held movement on a slow
connection. The left panel displays the currently effective amounts.

## Pivot and axes

The pivot is the mean of the selected native building origins. Nested groups expand
to individual members, with duplicates removed. Rotations move every member around
that shared pivot. X/Y/Z rotation axes are world-aligned; XYZ applies a simultaneous
angular step about the XYZ diagonal, with an exact inverse for downward scroll.

Scaling spreads member origins along the selected world axes about the pivot and
multiplies the corresponding local size components of each building. It does not
introduce shear into rotated buildings. The axis display uses red X, green Y, blue Z;
active rotation rings or scale arrows are bright and inactive axes are dim.

## Native data and current limits

Transforms operate on existing actors and lightweight instance records. They keep
inventories, recipe/customization data, runtime lightweight indices and blueprint
membership. Native instanced visuals, collision and spatial lookup data are moved
with the saved building transforms. Group references and pivots are updated in the
same operation; no permanent parent actor is created.

Currently supported: ordinary rigid buildings without connected conveyors, pipes or
power wires, and rigid lightweight structures such as foundations and walls.
Spline/network/resource-bound buildings, conveyor parts, pipelines, railways, wires
and train platforms are rejected. Disconnect external connections before moving a
machine. The entire selection is validated first, so a rejected member prevents a
partial transform. Connections are not automatically rebuilt or reconnected.

The server validates building identity, selection size, group revision, player state,
range, finite values and scale/world bounds. Steps are limited to 100 m translation,
180 degrees per axis from controls, and scale components between 0.01 and 100.
Transforms deliberately allow clipping; they do not run hologram placement rules.

Multiplayer replication and actual save/reload still require dedicated gameplay
coverage. Native transforms are the intended source of persistence; this does not
claim every building type or mod-removal scenario has been verified.
