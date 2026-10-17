# Volume selection

Volume is shared by Edit and Dismantle and appears in the native selection-mode
cycle and held-key radial. A building is included when its bounding-box center is
inside the volume, with 0.1 cm boundary tolerance. Ctrl uses the existing configured
multi-select action to pin or remove the current volume selection. Switching modes
retains pinned objects.

Default controls:

| Action | Binding |
| --- | --- |
| Switch sphere / cube | Alt+V |
| Grow / shrink | Scroll |
| Cube: adjust opposite face | Hold Alt while scrolling |
| Cube: rotate horizontally | Hold Shift while scrolling |
| Set / clear anchor | Alt+A |
| Volume settings | K |

The reference point follows the player, or the center of the anchor's bounds when
an anchor is present. In either Edit or Dismantle Volume, aim at a building and
press Alt+A (or the configured anchor binding) to anchor the volume to it. Aim at
that anchor and press the shortcut again to clear it and follow the player again.
Each entry into Volume starts with an 8 m sphere radius and
a cube with 16 m sides (8 m from the reference point to each face). Scrolling changes the sphere radius or one
cube face by 8 m by default. Cube faces use its rotated axes and whichever axis is
closest to the viewing direction, including looking up and down. Alt chooses the
opposite face while preserving scroll direction: positive grows, negative shrinks.
The radius and each face distance stay between 1 cm and 1 km from the reference point. Independent face adjustment
can make a rectangular box and move its geometric center away from the reference.
Rotation is around the reference point, in 15-degree yaw steps by default.

K provides temporary size and rotation steps. The mod settings provide their defaults.
The boundary color uses the native color picker under Options > User Interface >
Holograms > Building Skills volume boundary. The sphere and cube have continuous
translucent surfaces, visible from inside and outside. Their opacity is fixed.
The shape, wheel and modifier keys are rebindable.
Volume bindings are removed outside Volume; K remains available to other tools.

The query uses native actor and lightweight spatial indexes. More than 2,048
matching buildings yields an empty preview and a message requesting a smaller
volume, rather than silently dismantling a partial selection. The existing native
dismantle restrictions, refunds and server execution still apply.

See TESTING.md for version-specific validation and remaining dedicated-server checks.
