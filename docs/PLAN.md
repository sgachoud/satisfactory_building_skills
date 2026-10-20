# Plan gun

Alt+P opens the Plan gun. Its initial tool is the rail planner. Plan geometry is
local visualization, with no collision, costs or world buildings until Build.
Closing the gun hides all plan geometry without discarding the plan.

Personal plans belong to the native saved player identity. They are saved with
the world, and their geometry is sent only to their owner. Shared plans are
visible to everyone while using Plan, and everyone can edit or build them.
Shared edits use revision checks; a conflicting edit is rejected and the latest
plan is loaded instead of silently overwriting somebody else's work.

In **Saved plans**, clicking a name loads that plan immediately. There is no
separate Save or Load button. Double-click the name to rename it in place;
Enter or leaving the field commits the name. **Duplicate** creates and selects
an independent personal plan named `<plan name> (copy)`, including its geometry,
parallel offsets and build specification. The original remains unchanged.
Copies can be shared afterward. Names are limited to 80 characters, so a long
name is shortened as needed to retain the `(copy)` suffix.

Parallel lines reproduce the main nodes and keep them linked. Selecting or
moving a reproduced node edits the corresponding main node. Each parallel has
a signed **Node offset along local X**, and a signed **Rail start placement
offset**, both entered in meters with centimeter precision. Positive placement
offset leaves a gap at the path's start; negative offset extends backward along
the starting tangent. Placement offsets apply once per connected path between
branches, including across interior nodes that require a live rail connection.
The node markers remain at their planned positions even when the rail start
is trimmed. An offset that consumes the whole path prevents building.

## Controls

| Default | Action |
| --- | --- |
| Alt+P | Open / close Plan |
| R (native configured mode key) | Place / Edit; hold for the native mode selector |
| Left click in Place | Place a node and connect it to the previous node |
| Hold configured multi-select modifier (Ctrl) and aim | Select nodes and splines without clicking; start over a selected item to deselect |
| F | Delete selected nodes and splines, including splines attached to deleted nodes |
| Ctrl+Z / Ctrl+Y | Undo / redo plan geometry edits |
| Right click | End the current chain / clear selection |
| Left drag in Edit | Move a node or a Free spline's interior control |
| Scroll while dragging | Adjust camera-relative drag depth |
| Arrow keys | Move selected nodes and spline endpoints along the nearest horizontal axis to your gaze |
| Page Up / Page Down | Move along world Z |
| Alt+S | Stick Off / Default / Constrained |
| Scroll with Stick | Adjust distance, using the shared Stick step |
| Alt+scroll / Ctrl+scroll with Stick | Native Stick endpoint tilt controls |
| K | Named plans, selection geometry, parallel lines, Stick settings and Build |
| Escape | Close the menu first, then the Plan gun |

Plan runs in an empty build-gun state without creating a rail hologram. Off
uses the gun's surface trace; Default and Constrained use the same settings and
path calculations as Stick. Nearby existing rail endpoints can capture the aim
point; holding the configured multi-select modifier disables this snapping.
Foundation placement resolves lightweight instances before using the native rail
floor grid and rotation step. With Stick active, the aimed foundation must be
within 2 m of the tip. Holding the configured multi-select modifier bypasses
that floor snap.
The current Place/Edit mode appears in the native below-cursor mode row, and
holding R opens the native radial with both modes. The left panel shows the
current Stick distance instead of repeating that mode label.
Placing over an existing node reuses it and starts the next connection there.
Placing on a plan spline snaps onto the curve and splits it into two links, with
one shared node at the exact insertion point. The original curve shape is retained
for Curve, Straight, Free through-points and Bezier. The new node inherits the
curve tangent. Splitting, including an optional incoming branch, is one undo action.
In Stick modes, snapping remains near the stick tip so a curve between the player
and a distant tip does not steal the placement.
Extending an existing node inherits the actual adjoining spline tangent, including
at the new endpoint. Explicit Stick endpoint tilt can override the new direction;
snapping to another node or built rail uses that target's direction.
At either terminal, extension follows the outward signed tangent. A target behind
the node does not flip the curve by 180 degrees; constrained Stick previews use
that same direction. This geometric direction is independent of signal direction.
Adding an extension leaves existing node orientations and curves unchanged.
The oldest edge fixes the connected path's orientation, so extending its back
does not reverse arrows or move parallel offsets to the opposite side.
With exactly one spline selected, **Reverse spline direction** in K swaps its
start and end, reverses its control-point order and tangents, and preserves its
shape. Shared node orientations and other splines are untouched. This operation
uses the plan's existing undo/redo history and is separate from reversing signal
direction.
No self-edge or duplicate connection is added. Select a spline to move both its
endpoints; shared endpoints are moved once, even when multiple selected splines
use them.

Delete, Undo and Redo are rebindable and appear in the Plan hints. Each hold of
the selection modifier visits an item only once, so lingering over it does not
repeatedly toggle its selection.

Undo and Redo are also available in K. Each plan has a server-side session history
covering placement, movement, deletion, curve editing and parallel offsets. Shared
plans share this history. It uses the configured action history size (up to 256),
with a 16 MiB memory cap per plan; a new geometry edit clears redo. History is not
saved across sessions. Undo does not change plan names or sharing, and does not
remove rails already constructed by Build.

## K menu

Saved plans, Nodes and splines, selected items, parallel lines and Stick settings
are collapsible.
Their expanded/collapsed states are remembered on this computer per player account,
across menus and game sessions. These preferences are not part of the world save.

Select a named plan, then **Load** to edit it or **Make shared** to publish it to
the other players in this world. **New plan** creates a separate named plan;
**Save name** names or renames the current plan. Each accepted edit is already
part of the world's next save. Sharing does not copy or delete the owner's plan.

The **Nodes and splines** list shows every item in the current plan. Click to
select one; Ctrl+click adds or removes items. Selected rows and their world
geometry highlight together. The list is virtualized for large plans.

A single selected node exposes world X/Y/Z in meters with two decimal places,
and world yaw, roll and pitch in degrees. **Apply position and orientation**
submits one undoable edit. A node snapped to a built rail uses that connector's
tangent direction; moving it detaches it. Roll rotates the parallel-line frame
around the node tangent; native rails still use their native unbanked geometry.
A single spline exposes Curve, Straight, Free — through points and Free — Bezier
handles. Through points uses piecewise cubic Hermite curves with automatically
calculated interior tangents; the curve passes through each point. Points can be
added or removed. Bezier uses two handles, one for each endpoint, that pull the
curve without lying on it. Both kinds can be edited numerically or dragged in
Edit mode. Endpoints remain owned by nodes. Changing a Bezier handle updates the
shared node direction and adjoining curves to retain rail tangent continuity;
incompatible straight joins are marked invalid. Moving a Bezier endpoint carries
its associated handle along with it.

In Edit, nodes and through-points have a smaller direction dot joined by a straight
line. Drag the dot to rotate the tangent while the node/control point stays put;
scroll changes drag depth. Node edits align adjoining curve tangents, and one drag
is one undo action. Bezier control points retain their usual position handles and
do not receive extra direction dots.

Parallel offsets use local X forward, Y right and Z up in meters, with centimeter
precision. They follow the curve rather than fixed world axes. Add, edit and
remove offsets in K; the main path remains present. Each parallel has a Reverse
checkbox for its signal direction; its offset and geometry stay unchanged.

The **Build specification** section sets rail segment length in meters, initially
80 m, bounded by the native Railway minimum and maximum (with a 1 cm rounding
margin). Values commit when you finish typing or change a toggle; no Apply button
is needed. It offers three remainder policies:

- **Change only last:** try a short final piece. If it is below minimum, extend
  the preceding piece. If that exceeds maximum, divide their combined length
  into two equal pieces. A full path below minimum cannot be built.
- **Reduce evenly:** shorten all pieces equally to fit the complete path.
- **Increase evenly:** lengthen all pieces equally, without exceeding the native maximum.

Lengths apply across intermediate plan nodes. Divergences, open ends and nodes
at matching live rail endpoints force segment endpoints. Nodes save only their
geometry; removing a rail used while snapping does not invalidate the plan.
Construction discovers matching positions and opposing tangents in the current
world, including interior nodes. Each path between these
boundaries is divided separately; closed loops are processed once. An impossible
policy rejects the whole build before construction.

**Place block signal** defaults off. When enabled, **Every N segment(s)** defaults
to 1 and counts from the start of each path in the displayed signal direction.
**Left side signal** selects the native left-hand signal; right-hand is the default.
**Reverse direction** reverses signal direction for selected spline paths, or all
paths with no selection. Arrows show the resulting direction on primary and
parallel paths. Reversing never changes curve geometry or segment boundaries.
Signals go at connected segment endpoints, using native signal construction and
guarded connections. Unconnected open ends remain unsignalled, as required by
vanilla placement. Rail and signal costs are checked together before construction.
Build specifications and direction choices are saved per plan and support undo.

Preview segments turn red around excessive curvature, grade or discontinuous
node joins. **Build full plan** subdivides each complete path to native rail
length limits, validates all pieces and checks the complete material cost.
Affordability combines the player inventory and Dimensional Depot; payment follows
the player's inventory-first/Depot-first setting. A shortage reports the required
item and both available quantities. Each spline is processed once, including in
closed loops; construction tracks visited edges instead of recursively walking the network.
It constructs rails on the server and verifies reciprocal internal and existing
endpoint connections. Failed construction removes the incomplete batch. The plan
remains available after a successful build.

Limits are explicit: 32 plans per owner, 256 per world, 2048 nodes and splines per
plan, 64 captured curve points per spline, 32 interior controls per Free spline,
16 parallel lines, and 2048 built rail
segments per operation. Oversized operations are rejected rather than truncated.

## Validation

The 0.10.6 Windows package passed 197 Plan runtime assertions and all 25 Editor
tests. Coverage includes signed endpoint continuity, constrained extension from
the first node, native minimum/maximum lengths and short-tail fallback, live rail
connection discovery, closed loops, signals, inventory/Depot deductions and
automatic menu saving. The Build specification menu was inspected in a rendered
screenshot. The temporary game exited cleanly. See [TESTING.md](TESTING.md) for
exact coverage, manual checks and the limits of local permission checks versus a
live dedicated-server run.
