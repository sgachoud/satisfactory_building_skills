# Saving selections as blueprints

In normal Edit, select buildings, groups or a mixture and press **Alt+S / Save**.
The action is rebindable under Building Skills and appears in the native bottom
hint bar whenever the selection is nonempty. It is unavailable while transforming.

The native blueprint window provides name, description, icon, color, category and
overwrite confirmation. **Load and Clear are omitted**. Closing the window returns
to Edit with the selection intact. Saving includes nested group members once;
it creates a normal blueprint without removing or moving the original buildings.

An owner-only, transient save session supplies the native designer interface with
the selected members and cost. It creates no platform, storage or construction
volume and is never saved with the world. The normal game writer handles blueprint
files and descriptors. Lightweight members are resolved to their native temporary
actors immediately before serialization. Server validation checks ownership,
selection revision, building identity, size and player range again on Save.
If selected content changes while the window is open, close it and select it again.

In multiplayer, the temporary designer stays awake for replication. The client
matches it to the current request by a replicated identifier, rather than expecting
a newly spawned actor reference to be usable as soon as an RPC arrives. Requests
time out after 15 seconds with feedback and clean up their server session; late
replies cannot reopen a closed window or cancel a newer request. The save archive
and member transforms are validated before writing or transferring it to clients.

Load and Clear are also blocked in the native save-session methods so controller
shortcuts cannot place or dismantle buildings through this window. Ordinary
blueprint designers retain their normal functionality.

## Bounds, alignment and anchor

Without an anchor, the content stays upright and is rotated horizontally to align
the majority of buildings, then its bounding box fits tightly around the content.
The origin is centered in X/Y and at the lowest Z point of the bounds. Foundation
geometry includes its full width, depth and height; lightweight actors without mesh
components use the native instance geometry. Saved blueprint placement uses those
bounds for native hologram sizing, snapping and blueprint membership.

**Alt+A / Anchor** sets or clears the aimed building as the reference. This action
is configurable. The anchor supplies all three orientation axes, including tilt.
An unselected anchor is blue and is excluded from the saved content. Selecting it
with the configured multi-select action includes it and makes it purple. If an
excluded anchor extends lower than the selected content in that frame, its bottom
still defines the blueprint floor. The empty gap beneath the selected buildings
is retained through native hologram loading and snapping. With no anchor, automatic
horizontal alignment remains unchanged.

## Scale preservation

Native blueprint actor headers can store scale. Building Skills preserves that
scale when saving and restores it during native blueprint loading, including the
hologram and final placed buildings. This uses ordinary blueprint bytes, without a
separate scale file. Header handling is scoped to blueprint serialization so world
save serialization is unaffected. Each spawn consumes only its own header; nested
spawns cannot reuse the same header. Nonfinite, near-zero or extreme imported scales
are ignored; the supported import range is an absolute scale of 0.0001 to 10000 on
each axis (Edit's existing scale limits remain 0.01 to 100).

Ordinary actors use the existing bounded transform transport and transient late-join
snapshot to replicate their scale. Newly created lightweight instances use their
native full-transform data. Online behavior still requires runtime verification.

A vanilla loader accepts the native file structure but discards its scale during
placement. Buildings appear at normal size without Building Skills. Resaving such a
placement in vanilla also loses the custom scales. Older unit-scale blueprints keep
their existing behavior; already discarded scales cannot be recovered automatically.

Before group Save writes to disk, the completed object table must contain every selected actor
with its expected relative transform. Missing members or incorrect transforms reject
the write and report a failure instead of confirming a faulty blueprint. Native
archive/header and transfer behavior otherwise remains unchanged.

## Copy to the Build Gun

**Alt+C / Copy** is configurable under Building Skills and available for the same
nonempty normal-Edit selection as Save. It opens the native blueprint hologram
placement flow with a snapshot of the selection. Buildings do not need to belong
to a saved blueprint. Native placement handles building costs, snapping, rotation,
collision checks and construction; cancel placement using the usual Build Gun controls.

The copy uses the same automatic or anchored alignment, bottom-center origin and preserved scale
as Save. Selected groups retain their names and nesting after placement. Every new
group receives a fresh identity and references only the new buildings. Ungrouped
selected buildings remain ungrouped. Editing a copy therefore does not edit its source.

Clipboard archives and descriptors are transient, owned by the requesting player,
and omitted from the blueprint library and world saves. The server validates and
serializes the selection, then sends bounded, acknowledged chunks to that client.
Clients never upload building archives. Construction validates clipboard ownership.
The latest two snapshots are retained for placements already in flight. Restarting
the session clears the clipboard; use Save for a reusable blueprint.

Selection limits are 2,048 buildings and 32 MiB per clipboard snapshot. Online tests
must cover two players copying different selections simultaneously, normal material
costs, cancellation, repeat placement, scaled foundations, machines and nested groups.
The installed Windows regression passes native hologram construction, fresh nested
group identities, unchanged sources, and the configured C-key handoff. See
[TESTING.md](TESTING.md) for results and the remaining multiplayer checks. Copy
requires the selected building recipes to be unlocked, matching native blueprints.

## Network boundaries

New copy and blueprint placements restrict their building connections to the newly
loaded members before native connection cleanup and network rebuilding. Foreign
pipe ports, cable endpoints and circuit entries are cleared on the new components
only, preserving live source connections. Cached wire counts are refreshed as well.
The game's native serializer still handles internal links, pipe network registration
and the connection between both faces of double wall outlets.

There is no save migration, automatic disconnection or repair of existing factories.
These safeguards apply when saving/copying and placing blueprints; existing affected
pipes or outlets must be rebuilt manually. The reported background pressure-group
crash has not been reproduced by this fixture and is not yet attributed conclusively.

## Deduplication during placement

With a native blueprint or an Alt+C copy hologram active, **Alt+D / Toggle
deduplication** cycles **Off -> Replace -> Skip**. It starts Off and resets when
leaving blueprint placement. The configurable shortcut appears as **Toggle
deduplication** in the native Build Gun hint list for both saved blueprints and
copies. The active choice appears in the status indicator and is sent with each
placement to the server.

Matching requires the same structural building type, center and oriented bounding
box, with a 0.1 cm positional tolerance. Native material mappings identify variants
of the same building; swatches are ignored. Different heights, scales, diagonal
orientations or partial overlaps do not match. A square foundation rotated by a
quarter-turn still occupies the same box and matches.
Power cables use their saved relative endpoint envelope because preview cables have
no registered render mesh or initialized world-endpoint cache. Candidate cables are
found through nearby power ports. Pipes, belts and rails use their raw spline
curve bounds and mesh cross-section, consistently before and after BeginPlay.

Skip omits matched archive members and their ingredient costs; existing buildings
remain untouched. It prices the remaining members directly, excluding nested
refunds for skipped members. The price is cached until the set of overlapping
members changes. This avoids carrying over header costs from external source
connections; preview spline lengths are evaluated from the saved curves.
Groups from an Alt+C copy contain only newly placed members;
empty groups are removed. Replace constructs the new members, then dismantles the
matched old members using native refunds. A target that cannot be dismantled
independently rejects replacement before construction. Native dismantling disconnects
old external connections; deduplication does not rewire them to the replacement.

Save and Copy upload their selections through acknowledged packets of at most
32 entries, shared with grouping. Client and server both need version 0.9.5 for
this selection-transfer protocol; large selections are no longer sent as one RPC.
The Save-window timeout extends while upload acknowledgements arrive. See
[TESTING.md](TESTING.md) for the exact local tests and remaining multiplayer checks.

Blueprint placement clears saved conveyor-chain references on new belts and
lifts before native registration. A chain belongs to the running source factory;
it must not be reused by a placed copy. Native simulation builds independent
chains from the copied connections. This also applies to Copy and blueprint-based
restoration. Existing source chains and world saves are not rewritten.
