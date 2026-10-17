# Stick spline placement

Stick is an optional placement aid for conveyor belts, pipes, hypertubes and
railway tracks using the native spline hologram families. Other mods' subclasses
of those families use the same integration; unrelated custom placement systems
need their own adapter.

- **Alt+S** cycles Off, Default and Constrained. The binding is configurable and
  appears in the bottom-right shortcut helper while a spline is equipped.
- **Scroll** adjusts the camera-to-target distance, initially 64 m, with a 1 m
  default step and a minimum distance of zero.
- **Alt+scroll** tilts the free endpoint up/down in 15 degree steps by default.
  The modifier is rebindable. K changes the step for this save session; the mod
  settings supply its persistent default. Tilt ranges from -90 to +90 degrees,
  and returning from a non-spline tool resets it to automatic routing.
- **Ctrl+scroll** turns the free endpoint left/right, relative to its native route.
  It shares the 15 degree default and configured tilt step with vertical tilt.
  The modifier is rebindable and retains vanilla Ctrl's suppression of snapping.
  Vertical and horizontal tilt can be combined; ordinary scroll still changes
  distance. Both axes reset when leaving the spline tool.
- **K** opens Stick settings only while Stick is active on a spline hologram.
  It releases the binding when Stick is off or the gun changes tools.
- Turning Stick on or off keeps the existing start of the spline.

Default uses the native route for the selected build mode, then divides that
curve into equal-length pieces when it exceeds the building's length limit.
Dividing a curve preserves its shape and tangents.

Nearby compatible endpoints use the game's native snapping search and retain
the accepted position and direction. Holding the configured vanilla snapping
modifier (Ctrl by default) suppresses endpoint snapping while Stick is active,
including on rails. An accepted endpoint takes priority over free-space
Constrained projection, so its native route remains connected.

Split rail construction also verifies the first and last pieces against the
exact existing connectors accepted by native snapping. Missing reciprocal links
are completed through the native rail connection API; a failed connection rejects
the incomplete placement. Building Skills adds no proximity search for unsnapped
endpoints; vanilla automatic joins at coincident rail ends remain unchanged.
Construction messages carry accepted endpoints as existing rail actors and
connector indices, independently of the native hologram component references.
The receiver restores those snap targets, then validates their positions after
the native construction transform has been restored.
Client and server must both use 0.9.6 for this message format.

When Auto Rail Supports 1.1.0 is installed, its preview reads the complete Stick
route even when construction is split into several pieces. Supports remain
owned by ARS, including its enable toggle, blueprint choice, spacing and costs.
Building Skills does not change that mod's keybindings or configuration.

Constrained acts like Default until the start is placed. Afterward it finds the
nearest point to the stick tip on a path with the chosen upward slope and signed
horizontal radius. Slope uses linked degrees and percentage fields; calculations
use radians. Positive radius turns right, negative turns left, and zero means
straight. Zero slope and zero radius are the defaults. Nonzero slope and radius
form a helix; later turns are considered when finding the nearest point. The
equivalent degree and percentage fields sit side by side; radius has its own row.

K provides independent **Vertical constraint (slope)** and **Horizontal constraint
(radius)** checkboxes. Both initially apply. Unchecking one disables its numeric
fields without discarding their values. With only vertical enabled, the route can
turn horizontally toward the tip while keeping the selected grade. With only
horizontal enabled, the selected circle/straight heading remains constrained but
height follows the tip. With both unchecked, native routing is used, including
normal length splitting and endpoint tilt. These switches last for this save
session; Apply commits them and Cancel/Escape discards uncommitted changes.

The first constrained piece transitions smoothly from the actual starting
connector direction into the chosen slope/radius. When an endpoint tilt has
been selected, the final piece transitions to that pitch. These boundary pieces
may depart from the interior constraints to satisfy continuity; their positions
stay fixed. Any increased curve length is split again before native validation.
Default mode also honors the chosen free-end tilt. Snapping to an existing end
keeps that connector's direction instead of the manual tilt.

Segments are divided again when needed to keep a turn at or below 180 degrees.
The native placement rules still apply to every segment, including curvature,
grade, clearance and affordability. Paths requiring more than 256 segments are
rejected with a visible message rather than truncated. Stick-owned support poles
may float without requiring terrain beneath them. Ordinary support placement
retains its normal ground check.

Temporary settings belong to the world session and are discarded on leaving the
save. The mod settings provide persistent defaults for distance, distance step, tilt step, slope and
radius. Stick does not add persistent save records.

## Validation status

The installed 0.9.6 Windows build passed 311 rendered Stick checks, including
split routes connected to existing rails at both ends after deliberately clearing
the native component references before serialization, rotated construction
transforms, native final-placement clicks, unsnapped endpoint transport and
reciprocal cleanup. Existing snapping, tilt and support-spacing checks passed.
Windows and Linux archives were verified; live dedicated-server behavior still
needs multiplayer confirmation with matching versions.

The 0.7.0 editor path tests passed, including nearest points on later helix turns,
signed radius/slope, continuous joins, half-turn subdivision and preservation
of native cubic routes. The installed Windows candidate passed a rendered
isolated regression covering real Alt+S/K/wheel input, mouse editing and Apply,
linked percent/degrees, Escape, native construction-message roundtrips, costs,
length limits and reciprocal connections for straight and multi-turn paths in
all four supported families. The 0.7.1 rendered run passed 190 assertions,
including native first-endpoint placement, fixed start positions, exact
camera-tip/constrained endpoints, and native placement approval for long paths
and floating supports before and after a construction-message roundtrip.
Turning Stick off restores ordinary support ground checks. Windows and Linux packages passed binary-hash,
architecture and archive checks; the Windows installation was verified by hash.
The 0.7.2 run passed 213 assertions, adding native endpoint snap/Ctrl checks,
full-route support preview and placement with ARS 1.1.0, correct builder identity
at every rail spawn notification, and cleanup of expired blueprint previews at
shutdown. The 0.7.3 run passed 246 assertions, adding configured Alt+wheel tilt,
mouse editing of its temporary step, inclined-start continuity, boundary-angle
transport and repeated preview updates across all four families. Both platforms
were compiled and packaged, and the Windows installation was verified by hash.
A local serialization roundtrip is not an online server test. Linux gameplay
still requires testing on the server.

## Player checks

For split pipes, hypertubes and belts, the root hologram handles placement clicks
while the last generated segment owns the accepted destination connection.
During the native final-click handler only, the root sees that destination.
Its original segment connection is restored before serialization or construction;
the first segment must never connect directly to the far endpoint.

1. Equip a belt, pipe, hypertube or rail. Confirm Alt+S appears in the hints;
   activate Default and scroll around the initial 64 m distance.
2. Place the start, then cycle Stick off/on. The start and native build step
   should remain intact. Try the native build-mode choices with R.
3. With Stick active, open K, enter a slope as degrees or percent, and a signed
   radius in meters. Apply and switch to Constrained. Move your view and scroll
   to explore the closest point, including later turns of a spiral.
4. Place a long path and verify travel or flow through each internal join and
   its connections to existing buildings. Native invalid placement warnings
   still prevent construction.
5. Change tools and return: temporary settings remain for this save session.
   Turn Stick off and confirm ordinary wheel controls and other K menus return.

6. Move the stick tip near a compatible existing endpoint. Confirm snapping
   and correct endpoint orientation; hold the configured native Ctrl modifier
   to suppress snapping. Check both placement endpoints and both Stick modes.
   Place a route longer than one segment between two existing supports: the
   final click must build the entire connected route, without adding supports.
7. With optional rail supports enabled, preview and place a route longer than
   one rail. Confirm supports cover the whole route, follow that mod's settings,
   and charge the building player. Repeat with its feature disabled.
8. After using blueprint previews or Copy online, disconnect by quitting to
   desktop and check that the session closes normally.

9. Start from an inclined pipe or rail, use Constrained, and adjust Alt+scroll.
   The first piece should continue the connector smoothly and the final piece
   should have the displayed tilt. Confirm K changes the step without persisting
   it over a save reload, and ordinary scrolling still changes distance.
