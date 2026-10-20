# Verification

Verification uses CSS Unreal 5.6.1 and SML 3.12.0. The declared minimum game
build is CL 502094; the current installed Steam game is CL 513885 (1.2.4.1).
Results below are tied to their tested versions. Current development version: Building Skills 0.10.6.

## 0.10.6: Planner continuity, settings and action redo

All **25 Editor tests**, **197 packaged Plan assertions** and **70 packaged Polish
assertions** passed with zero failures. Both temporary-world runs exited cleanly.
The final Windows installation matches all eight archive files by SHA-256;
Windows and Linux Server archive binaries match the compiled outputs. No live
dedicated-server session was exercised.

The Plan run covers signed tangent continuation at either terminal, including
targets behind the existing tangent and a real constrained-Stick preview after
the camera updates. It checks the 80 m default, the menu minimum against an
initialized native hologram, and native construction for each short-tail fallback.
On the tested game, the usable range is 12.00–99.99 m. The final Build specification
screenshot was inspected. The first run's constrained-preview fixture read the
camera in the same frame as teleporting; the corrected test waits for its update.

The tests also cover menu field commits without an Apply button, a registered
Mods defaults editor with its widget tree, build-time discovery of an existing
rail at an interior node, and successful construction after deleting the original
snapping source. Editor input checks verify one registration per binding, shared
K activation in each scoped context, and restoration of another K action after
removing our context. Foundation snapping reuses the native floor helper; direct
in-game comparison of Ctrl floor snapping remains a manual check.

The Polish run adds construction redo, deletion redo and undo after each redo,
alongside restored blueprint identity, inventories/refunds, groups, power, pipes,
rails/signals and lift/belt connections. It ran before the final planner-only
minimum-length/test-fixture correction; the undo/redo implementation was unchanged.
Persistent mod-default editing was checked through its registered editor, not by
changing the player's saved configuration.

Evidence: `Saved/Automation/index.json`, `Saved/RuntimeTest0106-Plan.log`,
`Saved/RuntimeTest0106-Polish.log`, and the versioned verification manifests in
`Saved/Releases`. These generated files remain ignored.

## 0.10.5: Plan build specification, signals and construction fixes

All 25 Editor tests passed. The corrected installed Windows package passed **172
Plan runtime assertions**, with zero failures and a clean exit. All eight installed
files match the Windows archive by SHA-256. The Build specification screenshot
was inspected after the final run.

Coverage includes inherited extension directions and duplicate-route cleanup;
whole-path segmentation across intermediate nodes; forced endpoints at branches;
finite closed-loop traversal; all three remainder policies and atomic rejection;
block-signal spacing, right/left side and reversed parallel directions; saved and
transmitted build specifications; and exact combined rail/signal deductions from
inventory, Depot, split sources and both withdrawal preferences. Shortage and
No Build Cost cases are covered. Existing Plan controls and history remain tested.

The first packaged run caught native rejection at the exact rail maximum and
hidden signal previews being treated as invalid aim locations. The final build
uses a centimeter of maximum-length headroom and initializes signal previews
before native validation. Native signals require a connected rail joint, so open
ends remain unsignalled. These corrections passed the full packaged rerun.

The matching Linux server binary compiled and its cooked archive was hash-verified.
No live dedicated-server multiplayer test was run; local RPC/access checks do not
constitute one.

## 0.10.4: Plan splitting, selection list and orientation

All 25 Editor tests passed. The final installed Windows package passed all 112
Plan assertions in the rendered temporary world, with zero failures and a clean
exit. Windows installation hashes match all eight archive entries. The Linux
server binary and archive were built and hash-verified; no live dedicated-server
gameplay test is claimed.

Coverage includes exact split/projection checks for all four curve types, a split
at an existing through-point, atomic split history, shared-node tangent propagation,
manual through-point tangents and rolled parallel frames. The packaged fixture
exercises the virtualized selection list, real click-to-split, node and through-point
direction dragging, orientation undo, editable orientation fields, saved orientation
and captured constrained-Stick paths. Existing tap/hold R, collapsed-menu,
selection and connected rail construction checks remain enabled. The K-menu
screenshot was inspected, including selection rows and yaw/roll/pitch fields.

The first packaged run exposed a fixture timing issue: its click preceded the
camera update after teleportation, so it was not aiming at the spline. The fixture
now waits for the camera before clicking; the insertion and dependent drag checks
pass. Review also found that the placement command discarded the constrained-Stick
preview route. The command now retains that route, with a packaged regression.

## 0.10.3: Plan controls and local menu preferences

Short R presses reject the native deferred first-item assignment; explicit Plan
changes and held-radial choices still update the native mode descriptor. Horizontal
arrows follow the nearest camera-yaw axis; Page Up/Down stay on world Z. The K
menu has independent Saved plans, Selected items, Parallel lines and Stick
sections. Expansion states are stored locally under the local account identity
(or local platform user when offline), outside world saves and replication.

All 25 Editor tests passed. The final installed Windows candidate passed all 69
Plan assertions in the rendered temporary world and exited cleanly. Checks cover
a real short R key gesture after deferred callbacks, collapsing sections and
reloading their preferences from disk, movement with a turned/tilted camera, and
world-Z Page Up. Direction checks cover all four horizontal quadrants. The fixture
uses a separate local preferences file. The collapsed-menu screenshot was inspected.

The first packaged run caught a missing first-time preference-file write and a
movement direction read from the previous camera frame. Preferences now use a
dedicated local file, and movement uses the controller's current view rotation.
Both failing checks passed after targeted recompilation and reinstallation.
Windows installation hashes match all eight archive entries. Linux is packaged
separately; this does not constitute a live dedicated-server gameplay test.

## 0.10.2: Plan state, native modes and curve controls

Plan now uses an empty native build state and independent node aiming, instead
of creating and hiding a rail hologram. Place/Edit use native mode descriptors,
the below-cursor mode row and the native R radial. Free curves offer either
interpolating Hermite points or two cubic Bezier handles. Bezier endpoint moves
carry the attached handle; editing a handle aligns adjacent node tangents.

All 25 editor tests passed. The final installed Windows build passed 60 Plan
assertions in a rendered temporary world with saving blocked and exited cleanly.
Checks cover no rail holograms during long previews, native mode identity,
tap/hold R, Bezier geometry, handle movement, malformed curve rejection, selection,
history, plan access and connected rail construction. Place, K-menu and held-R
radial screenshots were inspected. Construction-only labels/hints are absent in
Plan, and the ordinary rail recipe creates its native hologram after leaving.

The first packaged run caught a shipped-game discrepancy: `GotoBuildState`
rejects a null recipe despite the SDK comment allowing it. The Plan-scoped
transition now clears the recipe and enters the native state directly. The
second run passed 59 checks and exposed a return-to-build assertion that wrongly
required the native hologram to be visible while aiming into empty space; it also
revealed overlapping construction-only HUD content. These were corrected before
final validation. The fixture now skips dependent checks safely if its initial
graph was not created, and captures the radial before releasing the mode key.

Windows installation is checked against all eight packaged files by SHA-256.
The Linux binary/package is built separately; no live dedicated-server gameplay
is claimed by the standalone permission checks above.

## 0.10.1: Plan selection, deletion and history

The update hides native rail preview meshes and split children while Plan is
active, selects nodes and curves by holding the configured multi-select modifier
without clicking, and adds rebindable F deletion and Ctrl+Z / Ctrl+Y history.
Undo/redo covers geometry edits, keeps current ownership and sharing, and checks
shared-plan revisions. Session history uses the action history setting and a
per-plan memory cap.

All 25 editor tests passed. The installed Windows package passed all 54 Plan
assertions in a rendered fresh world with saving blocked, including hold-only
node and spline selection, stable selection during a held gesture, deselection,
F deletion without changing guns, Ctrl+Z restoration, Ctrl+Y redo, and restoration
of ordinary rail holograms after leaving Plan. Native spline meshes and split
children were hidden during planning. Place and K-menu screenshots were inspected.

The first packaged run caught unreliable polling of the native multi-select
action value. Plan now reads its configured key and modifier chord through the
same native mapping lookup used by transform steps. The subsequent uninterrupted
run passed and exited cleanly. An intervening run was interrupted overnight and
timed out; it is not counted as a pass.

History tests cover stale revisions, private access, shared editing, incident
Free curves and controls, parallel offsets, action limits, redo invalidation and
preservation of current plan names/sharing. These access checks use separate
player-state identities in a standalone world, not a live dedicated server.
Windows installation is verified against all eight package files by SHA-256.
The final editor, Windows and Linux binaries compiled. Both release archives
contain the new controls and are checked against the current native binaries.
The Linux package has not been tested on a live dedicated server.

## 0.10.0: Plan gun and rail planner

The Plan gun adds owner-filtered saved rail graphs, shared-plan revision checks,
native rail/Stick aiming, selectable nodes and splines, world-axis movement,
editable Free controls, local parallel offsets and server-side rail construction.
The editor, Windows client and Linux server targets compiled. All 25 editor
automation tests passed, including Plan graph/geometry and input mappings.
The installed Windows package passed all 34 Plan assertions in the rendered,
fresh-world regression. The K menu screenshot was inspected with a selected
node: its world coordinates use meters with centimeter precision, and normal
Build HUD controls are hidden while planning.

The passing regression covers graph mutations, continuity, local shape limits,
native gun clicks, close/reopen persistence, save-archive round trips, private
versus shared snapshots, the K menu, full-path construction and reciprocal rail
joins. It constructed 12 rail segments across a main and parallel path. Native
Ctrl-click selection, arrow movement along world X, and held-R radial opening
and release also passed. Planning itself constructed no real rails; invalid
geometry was rejected before construction.

Runtime validation corrected tip snapping that previously caught the preceding
node on a straight chain, and removed an unsupported detour of a native no-op
mouse-release function. Linux packaging verifies the Plan input assets and the
compiled server binary. Windows installation is checked against all eight
archive entries by SHA-256.

The existing rendered Stick regression also passed all 368 assertions, including
typed temporary settings, endpoint tilt, native snapping, support spacing and
final clicks between existing supports. Both rendered runs exited cleanly.
The runner now requires rendering for Stick: its later tilt checks depend on a
temporary value entered through the real settings window.

These permission checks use separate native player identities in a standalone
world, not two networked clients. The Linux package has not been exercised on a
live dedicated server. Save persistence was checked through the native SaveGame
archive and closing/reopening Plan, not by loading an existing player save.

## 0.9.8: final click on a snapped split route

The October 17 solo report narrows the failure to a valid hypertube preview
between existing supports, spanning more than one segment. Splitting transfers
the destination snap to the last generated hologram. The native pipe/belt
final-click state machine still checks the root's destination, so it attempts
to advance a disabled support hologram instead of completing placement.

The correction temporarily exposes the accepted destination to the root only
inside the native click handler, then restores its segment connection before
serialization and construction. Single segments, free endpoints and normal
non-Stick placement keep their existing behavior.

The new fixture retains the two actual poles from an earlier placement,
removes its splines, snaps a new two-segment route to both poles and presses the
native build-gun primary action. It exercises pipes, hypertubes and belts,
including the unadapted native click as a baseline. The fixture moves the player
near each support before aiming, allowing native significance to enable the
support's collision for the 150 m hypertube route. The shorter pipe/belt
fixtures retain their valid native aiming approach from the first support.

Both native targets compiled and linked successfully. An initial rendered run
reproduced the baseline stall and completed the corrected pipe and belt routes.
Its hypertube aim check missed the distant support while the player remained
150 metres away, then placed a new support; that run is retained as a failure
in `Saved/Runtime098-EndpointInitial.log`. A near-destination variant confirmed
the hypertube fix, but changed the belt's native route into an invalid shape;
that failed fixture is retained in `Saved/Runtime098-EndpointNear.log`. The
final combined fixture uses the successful native approach for each family.
The final installed package passed all 368 Stick assertions with no failures
and a normal exit: `Saved/Runtime098-EndpointFinal.log`. In each of the three
families, the unadapted native handler reproduced the stalled final click;
the corrected native build-gun action then constructed two connected segments
with both outer endpoints attached to the existing supports.

Windows and Linux Server archives reuse the verified 0.9.7 cooked content
byte-for-byte; only native binaries, debug symbols and version metadata differ.
Archive integrity, required assets and unchanged entries were verified, and
all eight installed Windows files matched the final archive by SHA-256 before
the passing run. Evidence: `Saved/Build098-Endpoint.log`,
`Saved/Build098-EndpointLink.log`, `Saved/Build098-EndpointLinux.log`,
`Saved/Releases/BuildingSkills-0.9.8-verification.json` and
`Saved/Releases/BuildingSkills-0.9.8-installation.json`.
Linux compiled and packaged successfully; this local test does not establish
dedicated-server behavior.

## 0.9.8-diagnostic.1: multiplayer Stick placement cancellation

The October 16 report affects Default Stick hypertubes with both automatic
supports and existing endpoints. The supplied server logs cover 0.9.6 and 0.9.7.
Neither contains the incomplete-segment rollback error. The newer log has a
native lightweight construction-deserialization error near concrete pillar
references, but no evidence links that message to the reported tube placement.

Additional isolated checks complete the native support-placement clicks, copy
the hologram through the native relative-reference construction archive, validate
the receiving hologram and construct its segments. Default pipe and hypertube
cases passed with single and multiple segments; the long Default belt case also
passed. These are standalone Windows checks, not a client/server session.
Evidence: `Saved/Runtime098-NativeCopy.log`. That exploratory run failed other
checks (headless input, invalid constrained geometry and a short belt fixture),
so it is not an overall passing regression or proof of a fix.

The revised rendered run passed all 347 Stick assertions and exited normally:
`Saved/Runtime098-Pending.log`. Nine added Default cases cover supported
pipe/hypertube single and split placements and split belts, including another
native serialization round trip through a pending preview. All nine finished
the click sequence, passed receiver validation and constructed their expected
connected segment counts. Invalid exploratory geometry is not counted as a
successful supported placement. The run used the packaged diagnostic native
binary with the unchanged 0.9.7 cooked content; the diagnostic version metadata
was installed afterwards and all eight installed files were hash-verified.

The development diagnostics log received spline construction messages and
per-hologram rejection reasons only during the server construction request.
They do not bypass placement validation. The cancellation remains unconfirmed
until it is reproduced or the remote server supplies the rejection reason.

Both native targets compile and link. Diagnostic archives reuse the verified
0.9.7 cooked content unchanged; only native binaries, debug symbols and version
metadata differ. ZIP integrity, x86-64 architecture, required cooked assets and
all unchanged entries were checked against the retained baseline. Evidence:
`Saved/Build098-Pending.log`, `Saved/Build098-PendingLink.log`,
`Saved/Build098-Linux.log` and
`Saved/Releases/BuildingSkills-0.9.8-diagnostic.1-verification.json`.

## 0.9.7: Blueprint conveyor-chain isolation

The October 16 server report fails in `AFGConveyorChainActor::Factory_Tick`
after placement of a 993-building blueprint. The supplied archive contains 32
belts, 19 lifts, and saved references to original conveyor-chain actors. The
crash stack does not establish which mod caused the invalid state. Power-circuit
warnings also occur at server startup and are not proof of this crash's cause.

The fix clears saved conveyor-chain references on newly deserialized belts
and lifts before their native BeginPlay/registration, without reverting or
modifying source chains. The runtime fixture confirmed that copying a running
network resolved 51 references to the original chains; all 51 were cleared before
registration, and the copies acquired independent native simulation chains.

`RuntimeTest.ps1 -Render -BlueprintFixture <copied-blueprint.sbp>` runs the
opt-in regression in a new, save-blocked world. It uses native file decoding,
checks every required building class is installed, places the archive, then
captures and places its running conveyor network again. Checks cover independent
native chains, reciprocal copied ports, unchanged source connections, garbage
collection, and dismantling the copy. The input blueprint is read only and is
never imported into the player's blueprint library. The final installed package
passed all 18 assertions with a normal exit using the retained crash-time Copper
Factory V1 archive (993 buildings, including its gantry/train-station content).
Evidence: `Saved/Runtime097-CopperFinal.log`.

The final package also passed all 171 network assertions and 66 transform/undo
and large-selection checks, plus two shared queue assertions, with no failures
and normal exits. These cover pipe and power copies, double outlets, conveyor
simulation, connected lift deletion/restoration, and 900-object operations.
Evidence: `Saved/Runtime097-NetworksFinal.log` and `Saved/Runtime097-Polish.log`.
All runs used fresh temporary worlds with saving blocked.

An initial package omitted the plugin's cooked assets because the shared editor
receipt did not enable Building Skills. A broader test then failed in its setup
because the group subsystem was absent. After regenerating the editor receipt
and recooking both platforms, the final packages passed the tests above.
Packaging now checks the editor receipt, and installation verification requires
the root and input assets as well as matching file hashes. The asset check also
correctly rejected the retained incomplete package.

Windows and Linux Server compiled and linked successfully. Both final archives
passed integrity, version, architecture, native-binary hash and required-content
checks; all eight installed Windows files matched the package. Evidence:
`Saved/Build097-DiagnosticWindows.log`, `Saved/Build097-DiagnosticLinux.log`,
`Saved/Build097-FullContentInstall.log`, `Saved/Build097-FullContentLinux.log`,
`Saved/Releases/BuildingSkills-0.9.7-verification.json` and
`Saved/InstallationVerification.json`.

These are local Windows gameplay checks. The reported dedicated-server fatal
was not reproduced, so its exact cause and the fix's effect on that server still
need online confirmation. The Linux package was built and verified, not run on
a live server. No existing player save was loaded or modified.

## 0.9.6: Split rail endpoints

Added explicit rail-actor/connector-index transport for the accepted start/end
snap targets, receiver-side position checks, and reciprocal connection checks
after split construction. The regression constructs straight and curved split
routes between existing rails, clears native component references before the
message round trip, checks both outside joins, checks cleanup, and verifies that
the message preserves deliberately unsnapped endpoints. Another check follows
native snapping through the final placement clicks and message round trip.
The installed Windows 0.9.6 package passed all 311 rendered Stick assertions
with no failures and a normal exit. This includes both existing rail endpoints,
missing native component references, a rotated route received at a different
spawn position, final placement clicks, deliberately unsnapped message endpoints,
reciprocal cleanup, Ctrl suppression, both tilt controls, and 50 support tiles
with seven every-eighth-tile pillars. Evidence: `Saved/Runtime096-Release.log`.
The run used a fresh temporary world with saving blocked.

Both platform binaries compiled and both archives passed integrity, version,
architecture and compiled-binary hash checks. All eight installed Windows files
matched the package. Evidence: `Saved/Build096-ReleaseNative.log`,
`Saved/Build096-ReleaseInstall.log`, `Saved/Build096-ReleaseLinuxNative.log`,
`Saved/Build096-ReleaseLinuxPackage.log`,
`Saved/Releases/BuildingSkills-0.9.6-verification.json` and
`Saved/InstallationVerification.json`.

These connection-message round trips ran locally, not over a live network.
The reported dedicated-server case still needs multiplayer confirmation with
0.9.6 installed on both client and server. This update changes the Stick rail
construction message format.

## 0.9.5: Large selection requests

The client log confirmed rejected RPCs of approximately 87 KB for Group and
102 KB for a 900-object Save request, above Unreal's 65,536-byte message cap.
Group/Merge/Ungroup, Save and Copy now upload bounded, acknowledged selections;
large group-operation results use the same bounded format in the reverse direction.
Added protocol tests for chunk boundaries, exact reconstruction, truncation,
duplicate/out-of-order packets, metadata changes and oversized totals. Added
packaged checks for Group, Ungroup, Save-window opening, native blueprint archive
contents and Copy with 900 foundations, plus timeout recovery.

The editor build succeeded and all 24 automation tests passed with no test
warnings or failures, including `BuildingSkills.Groups.BoundedSelectionRequests`.
Evidence: `Saved/Automation095.json`.

The final installed Windows package passed all 66 rendered transform/undo and
large-selection assertions, with no failures and a normal exit. Group and Ungroup
retained all 900 foundations; Save opened the native window and the native archive
contained 900 building headers; Copy captured all 900 objects. A late reply was
ignored and timeout recovery retained the selection. Existing lift deletion/undo
checks also passed. Evidence: `Saved/Runtime095-Final.log`. The run used a fresh
temporary world with saving blocked and did not load or alter an existing save.

Windows and Linux Server 0.9.5 packages passed ZIP integrity, version, x86-64
architecture and compiled-binary SHA-256 checks. The installed Windows mod matched
all eight packaged files after both builds. Evidence:
`Saved/Build095-FinalInstall.log`, `Saved/Build095-LinuxNative.log`,
`Saved/Build095-LinuxPackage.log`,
`Saved/Releases/BuildingSkills-0.9.5-verification.json` and
`Saved/InstallationVerification.json`.

The local run exercises the chunk and acknowledgement methods without an actual
network link. Dedicated-server delivery, latency and relevance behavior still
require multiplayer verification with 0.9.5 on both client and server.

## 0.9.4: Lift deletion archive safety

The reported Linux server crash occurred in the deletion undo snapshot, inside
`FObjectWriterFName`, after the blueprint bulk-object hook. That hook incorrectly
treated whole archive objects as property references and could pass null to the
native writer. The hook now preserves valid dependency objects and rejects an
invalid entry without retaining or writing the partial snapshot. Capture also
rejects actors already dismantled but still alive for their removal effect.

The installed Windows 0.9.4 package passed all 58 transform/undo assertions on
October 15, 2026, including native dependency serialization, null-entry rejection,
a lift connected to two belts, single and batch deletion, capture rejection during
the dismantle effect, and undo restoration of internal and external connections.
Restored lifts retained their height, visibility and collision. The rendered run
used a fresh temporary world with saving blocked and exited normally. Evidence:
`Saved/Runtime094-Polish.log`.

The separate rendered network run passed 171 assertions with zero failures and
a normal exit. This includes copied double power outlets, cables, constructors,
pumps and pipes, preservation of the original networks, and thirty seconds of
native simulation on the copied circuits and fluid network. Evidence:
`Saved/Runtime094-Networks.log`. Both runs used the installed Steam client.

Windows and Linux Server 0.9.4 packages passed ZIP integrity, version, x86-64
architecture and compiled-binary SHA-256 verification. All eight installed Windows
files matched the package again after the Linux build. Evidence:
`Saved/Build094-Install.log`, `Saved/Build094-Linux.log`,
`Saved/Releases/BuildingSkills-0.9.4-verification.json` and
`Saved/InstallationVerification.json`. The reported Linux dedicated-server
scenario has not yet been replayed on the user's server.

## 0.9.3: Input/output-aware mirroring

Validated on October 13, 2026 on Steam CL 513885. The final installed Windows
package passed 108 transformer assertions with zero failures and a normal process
exit. The test used a fresh temporary world with saving blocked; no existing save
was loaded or modified. Native compilation and installation passed, and all eight
installed files matched the package by SHA-256.

The added checks verify constructor input/output positions and directions at yaw
0, 37 and 180 degrees, including the reported case where both outputs must face
the mirror. A mirrored constructor/belt assembly retains output-to-input links,
coincident belt/port endpoints, original connections, and subsequent matching.
Normal and smart splitters retain all three mirrored outlet connections; smart
splitter filters follow the corresponding outlet. Existing construction, paint,
deletion, connected-pipe, cable, transform and undo checks also passed.

The first candidate passed 105 checks. The final rerun adds the smart-splitter
routing safeguard; only the affected native unit needed recompilation for that
addition. No editor automation was rerun. Dedicated-server multiplayer still
requires an online gameplay test. Existing incorrectly oriented copies are not
automatically migrated.

Linux server compilation and packaging passed. Both release archives passed CRC,
version 0.9.3, x86-64 architecture and compiled-binary SHA-256 checks. The installed
Windows package was verified again after both builds.

## 0.9.2: Transformer-relative movement

Validated on October 12, 2026 against the installed Windows package on Steam
CL 513885: 94 transformer assertions and 45 transform/undo assertions passed,
with zero failures and normal process exits. Both runs used fresh temporary
worlds with saving blocked; existing saves were not loaded or changed. The
updated settings footer was also checked in the rendered screenshot.

Coverage includes all ten rotation/scale modes, transformer-relative translation,
explicit-selection precedence, disabled repetition, undo/redo after removing
transformers, constructor counterparts with attached cables, connected pipe
counterparts, native cable/spline restoration, and ordered transformer chains.
The existing construction, deletion, material and swatch tests passed as part
of the same transformer suite. Counterpart changes preserve the explicit selection
and join the original transform transaction before connected geometry is planned.

The Windows build and installation succeeded; all eight installed package files
matched SHA-256. The initial compile found a naming conflict in the new fixture;
after correcting it, the affected unit was rebuilt and the successful unchanged
units reused. No editor automation was rerun for this native-only change.
Linux server compilation and packaging also passed. Both versioned archives were
checked for CRC integrity, version 0.9.2, x86-64 binary format and SHA-256 equality
with the compiled binaries. Dedicated-server multiplayer behavior still needs an
online gameplay test.

## 0.9.1: Transformer settings, matching actions and immediate deletion restore

Validated on October 12, 2026. The settings/schema editor build and all 23 editor
automation tests passed. The final Windows native build and installation passed;
all eight installed files matched the package by SHA-256.

Rendered, isolated gameplay checks on Steam CL 513885 passed 55 transformer,
45 undo/polish, and 94 Connected assertions. The undo suite passed before the
last paint-only and cable-matching corrections; the transformer and Connected
suites were rerun against the final Windows package. Two external-mod K-menu
checks were skipped because an existing external binding owns K. No other mod's
bindings were changed. The settings screenshot was inspected. These tests used
temporary worlds and did not load or write existing saves.

Testing found and fixed native material replacement resetting swatches, cable
matching that assumed mirrored sockets moved with mesh reflection, and a
redundant dismantle-interface cleanup hook that crashed in a shared hook chain.
Actor destruction and lightweight removal still prune empty groups. Earlier
failed candidate runs are not counted as successful validation.

Verified behavior:

- The shared Build as blueprint checkbox applies to the entire transformer list,
  including an empty list, and combines the original placement and successful
  copies into one native blueprint selection. Undo removes that action together.
- Reassigning blueprint membership handles both lightweight and actor buildings,
  cleans up empty old proxies, and leaves Building Skills group nesting intact.
- Position (cm) and rotation (degrees) can be edited in world or original-anchor
  coordinates. The original anchor remains a fixed snapshot, including tilt.
  Switching coordinate display alone does not move the transformer.
- Apply updates the authoritative pose and shared setting together. Cancel, stale
  revisions and malformed requests do not apply partial settings.
- Deletion undo restores visible, collidable buildings without a build-effect
  delay, while retaining contents, connections, groups and blueprint identity.
- Transformer deletion, material and swatch actions find structural counterparts
  despite different materials or swatches, reject different scale or geometry,
  respect radius/repetition settings, and leave nonmatching buildings untouched.
- Default and Connected paint preview/apply matching counterparts through native
  customization. Replicated deletion is one undo action.
- Material replacement preserves persistent swatches. Pipe geometry mismatches
  and ambiguous overlapping counterparts are rejected; mirrored cables match
  corresponding sockets on transformed machines.

The final Linux server build and package passed. Both release archives passed
CRC, version, architecture and fresh native-binary hash checks, and the installed
Windows files were verified again afterward. The Linux package was not launched
on a dedicated server. Remote multiplayer behavior has not been exercised by
these local fixtures; use matching client/server versions for online checks.

## 0.9.0: Session construction transformers

Validated on October 12, 2026. Windows client, Linux server and editor builds
passed. All 22 editor automation tests passed, including transformer geometry,
positive scale/handedness, tilted frames, ordered expansion, radius boundaries,
copy limits and transient session storage.

The rendered, isolated Windows regression passed 32 transformer assertions:
anchor-gated Alt+X, physical K and mouse input, server settings/frame validation,
ordinary and blueprint hologram meshes, native hologram stencil rendering,
ordered construction, material charges, positive-scale reflection, physical
one-action undo, clearing previews, reciprocal isolated pipe connections,
separately mirrored constructors joined by a later mirrored cable, missing cable
endpoints, unchanged mirror-plane foundations, native blueprint selection identity
and nested group preservation. Screenshots of the settings window and both types
of hologram were inspected. The temporary world exited normally; existing saves
were not loaded or written.

Testing found and fixed exact overlapping lightweight copies that made selection
identity ambiguous, and missing stencil state that made preview meshes look
solid. Earlier failed candidate runs are not counted as successful validation.

Both release archives passed CRC, version, architecture and native-binary hash
checks. Version 0.9.0 was installed locally; all eight installed files matched
the Windows archive by SHA-256. The Linux archive was built and verified but was
not run on a dedicated server. The local fixture does not establish compatibility
with every building type or every combination of installed mods.

For multiplayer acceptance, use matching client/server versions: give two players
different transformer lists, place ordinary buildings and blueprints, reorder a
mirror/rotation chain, check copied connections and undo, then reconnect and
confirm session lists are cleared while constructed buildings remain.

## 0.8.0: Edit polish and construction/dismantle undo

Windows, Linux server and editor compilation passed. The rendered, isolated
polish regression passed 43 assertions: empty-group pruning, local and anchor
axes, world/anchor-relative pose display, reference labels, physical Ctrl+Z in
both guns, one history entry per batch, exact storage contents and refund
accounting, native blueprint identity, external power/pipe reconnection and
rail/signal ownership. It also checked that changed external connections and
spent refunds reject restoration without consuming the history entry.

The Windows candidate was installed and verified against its archive. Both
release archives passed integrity, version, architecture and fresh-binary checks.
These gameplay checks used a fresh local world; remote multiplayer behavior was
not exercised by that harness.

## 0.7.6: Horizontal Stick endpoint tilt

Ctrl+scroll turns the free endpoint horizontally using the existing configurable
endpoint tilt step. Alt+scroll retains vertical tilt. Each modifier is rebindable;
the horizontal action is non-consuming so native Ctrl still suppresses snapping.
Both axes appear in the hints/status and reset when leaving the spline tool.
Native endpoint snapping continues to take priority over manual tilt.

Added math coverage checks independent pitch/yaw, yaw wrapping and unchanged native
direction at zero offset. The isolated Stick fixture adds all-family combined
endpoint direction checks and a physical Ctrl+wheel check that distance and pitch
remain unchanged. Windows, Linux server and editor builds passed, together with
all 17 editor automation tests. The input asset has a rebindable Left Ctrl mapping,
non-consuming behavior and a native shortcut hint. Both archives passed integrity,
version, architecture and fresh-binary checks. The Windows package was installed
and all eight installed files matched its SHA-256 hashes. Satisfactory was not
launched; physical-input and packaged gameplay checks remain pending.

## 0.7.5: Multiplayer transform latency

Transform batches update group member positions and pivots without publishing a
new membership revision. This avoids downloading the entire group tree after each
move, including moves of an ungrouped foundation. Existing outline proxies move
with their building. Client selection references and delayed group snapshots are
reconciled with successfully applied transforms; stale-edit checks remain active.

Added automation covers nested pivots, consecutive deltas, out-of-order delivery,
a delayed topology snapshot, stale runtime-index hints and unrelated reused slots.
The isolated network fixture also asserts that transforms advance building versions
without changing group topology and that existing outline proxies move immediately.
Windows client, Linux server and editor compilation passed. All 17 Unreal editor
automation tests passed, including `Transform.GroupDeltaCatchUp` and `Stick.Path`.
Both versioned archives passed CRC, version, architecture and fresh-binary SHA-256
checks. Packaged gameplay and remote Linux/client latency checks are deferred while
the player uses the game. The 0.7.5 client was subsequently installed at the
user's request without tests or a game launch. Matching client/server packages
are required to exercise the new transform replication behavior.

Dedicated-server check: with matching client/server versions and an existing group
tree, repeatedly move one ungrouped foundation, then a nested group. Each movement
must settle without a new full group snapshot. Verify highlights follow, including
rotation, scaling, undo/redo, and that conflicting edits by another player still
require reviewing the selection. Join while a group is being moved and confirm the
final selection positions and group pivots match the world.

## 0.7.4: Independent Stick constraints

K has separate session-only vertical and horizontal constraint switches. Both
start enabled; disabled values are preserved. Vertical-only projection permits
free horizontal heading at the configured grade; horizontal-only projection
retains the selected radius/heading while allowing target height. With neither
constraint enabled, native routing and ordinary length splitting apply.
Start-connection continuity, explicit end tilt and native snapping retain their
existing priority. No construction-message fields or persistent save fields change.

Math regressions cover the two individual constraints, off-axis targets, retained
values, re-enabling and the vertical-only displacement edge case. The rendered
regression also checks all four constraint combinations and start continuity on
native belt, pipe, hypertube and rail holograms, along with actual checkbox
clicks, Apply, reopening, and Cancel.
The projection functions passed 70 standalone C++ checks using extracted source
and minimal math/vector adapters. This validates the projection calculations, not
Unreal routing or gameplay. Windows client and Linux server compilation passed.
Both packages passed CRC, version, architecture and fresh-binary SHA-256 checks.
Installation and rendered gameplay validation
are deferred while the player continues using the game.

## ARS pillar-spacing investigation after 0.7.3

Test-only diagnostics expand the previous single-blueprint support fixture to
fifty tiles with an every-eight pillar interval. A rendered isolated run using
copies of the Ring Tracks preset's three native blueprints passed 257 assertions:
50 Half Ring Supports, seven Full Ring Complements (35 members each), and seven
Ring Pillars (six members each), across four connected native rail pieces.
The same exact preset also passed after serializing the route and reconstructing
it in a fresh receiving-side hologram: four pieces retained the construction
player and produced the same 50/7/7 stamp counts. This is a local construction
message roundtrip, not a remote Linux-client test.
Double-rail alignment and combined blueprint membership were enabled. No real
save was loaded; the temporary blueprint copies were removed after the run.

A separate two-slot fixture placed 50 deck actors and seven actors in each pillar
slot, all under one blueprint proxy. Its initial one-centimeter interval check
was too strict for native spline reparameterization: measured error reached
3.6 cm over 64 m. The regression now permits 5 cm, still far below one tile, and the corrected
combined-blueprint run passed with all 64 actors and one shared proxy.
The verified release 0.7.3 was restored after the diagnostic runs; all eight
installed files matched its preserved archive by SHA-256.
The reported multiplayer missing-pillar problem has not been reproduced by these
local checks. Both reported client and server versions were 0.7.3; no production
placement fix is claimed from this investigation.

The optional `-ARSDoubleTest` and `-ARSPresetDirectory` arguments on
`scripts/RuntimeTest.ps1 -StickOnly -Render` enable these isolated fixtures.
The preset directory contains `settings.json` and its referenced native blueprint
file pairs. These are test inputs and are not distributed with the plugin.

## 0.7.3: Endpoint tilt and connection continuity

A rebindable Alt modifier switches Stick scrolling from distance to endpoint
pitch. The default step is 15 degrees; K provides a temporary step and mod
settings provide its persistent default. The HUD distinguishes automatic routing
from a selected angle. Existing snapped connectors keep their native direction.

Constrained boundary pieces receive cubic corrections with zero displacement
at their endpoints and the requested start/end derivatives. Interior pieces
retain their slope/radius; modified pieces are split again using their actual
curve length. Construction messages carry the resulting native geometry, so
this feature adds no save fields or new construction-message fields.

Windows and Linux Server compilation and packaging passed. The installed Windows
0.7.3 package passed **246 runtime assertions, zero failures**, in a rendered
isolated world with saving blocked, followed by normal process exit. Coverage
includes an inclined real starting pipe, Alt+wheel key events, the editable K
step and mouse Apply, both Stick modes across all four spline families,
construction-message preservation of boundary tilts, and continuous connected
splits within native length limits. Curved fixtures explicitly require a valid
nonzero path along the actual starting direction. Existing endpoint snapping,
Ctrl suppression, rail-support placement and blueprint shutdown checks passed.

Both archives passed CRC, descriptor/version, architecture and native binary
hash checks; all eight installed Windows files matched the archive by SHA-256.
The menu and shortcut screenshots were visually inspected. This is a local
construction-message roundtrip, not an online Linux gameplay test.

## 0.7.2: Native snapping, rail supports and shutdown

Stick retains accepted native connection snaps, including their direction and
route, instead of correcting them back to the free-space target. The configured
vanilla snapping modifier remains responsible for suppressing these snaps.

Optional Auto Rail Supports integration exposes the complete Stick curve during
its client preview update, then restores the first native construction piece.
Dynamic children also inherit the server root's construction player. A scoped
reference supplies that player during the early native spawn notification,
which fires before the hologram assigns its build-effect owner. The reference
is restored before actor configuration; the native event is not repeated. No ARS
configuration or installed files are changed. Compatibility was tested
against installed ARS 1.1.0 with a temporary native support blueprint that is
deleted after the isolated test.

Native blueprint shutdown calls Destroy on preview entries without checking for
null. The new guard removes expired entries before that loop while preserving
valid actors for native cleanup. The supplied stack is consistent with this
failure, but does not by itself identify which preview consumer destroyed an
actor. The regression deliberately leaves a null preview entry before quitting.

Windows and Linux compilation and packaging passed. The installed Windows
package passed **213 assertions**, with zero failed assertions, no fatal error
and a normal exit. The rendered fresh-world run covered native endpoint snapping
and configured Ctrl suppression for pipes, hypertubes, belts and rails, alongside
the existing placement, UI and construction-message regressions. Endpoint
fixtures move the player near the destination so native significance activates
its collision; Stick does not bypass native connection searches.

With ARS 1.1.0 enabled, the preview covered more than one rail length, three rail
pieces each emitted one native notification with the correct building player,
and four support buildings were placed. The first-piece spline was restored
after preview. The deliberately expired blueprint preview entry was removed at
shutdown and the game exited normally. Evidence: `Saved/Runtime072-Stick-final.log`
and `Saved/Runtime072-results.json`. The support fixture's temporary native
blueprint was deleted. No existing player save was loaded or written.

Both archives passed CRC, version, architecture and native-binary SHA-256 checks.
All eight installed Windows files match the archive. Evidence:
`Saved/Releases/BuildingSkills-0.7.2-verification.json` and
`Saved/InstallationVerification.json`. The Linux archive is built and verified;
online Linux gameplay and the user's exact multiplayer shutdown session have
not been reproduced. The isolated early-event ownership check verifies the
player reference but is not a multiple-player server test.

## 0.7.1: Stick placement approval and controls

The 0.7.0 construction fixtures did not call native placement validation before
constructing. Their connection checks passed while generated hidden holograms
could still be rejected by the actual Build Gun. The regression now supplies
materials and runs `ValidatePlacementAndCost`. Bare fixtures check generated
segments while preserving the root's native start-support requirement. Equipped
Build Gun fixtures check `CanConstruct` on the whole preview, including native
supports, and on its construction-message roundtrip. The receiving fixture
resets disqualifiers before deserialization, matching the native server order.

Generated segments are explicitly visible before native validation. Once their
geometry is ready, newly deserialized segments clear only the initialization
flag. During native pipe/belt validation, the preceding segment supplies a
temporary snapped start; that reference is removed before serialization and
construction. Real built connections are joined after construction. Floating
support poles skip only their floor check while owned by a Stick placement;
ordinary placement retains it. A one-bit flag on supported root spline
construction messages carries that permission to server validation. Its hook
uses the construction interface's own pointer type for correct secondary-base
adjustment on each platform. This is transient construction data, not save data.
Client and server must both use 0.7.1 or a compatible later version.

The initial distance is 64 m. The equivalent slope fields share a row, while
horizontal radius remains separate. The native bottom-right shortcut helper
shows the configured Stick binding above Edit while a spline is equipped.
Windows and Linux compilation and packaging passed. The installed Windows
package passed **190 assertions**, with zero failures, no fatal errors and a
normal exit. Coverage includes straight and multi-turn generated segments,
long equipped routes for all four families, first-frame received validation,
floating supports, and restoration of native ground checks when Stick is off.
The rendered run also checked the 64 m initial value, physical controls, mouse
editing, linked slope values and their side-by-side layout. Both hint and menu
screenshots were visually inspected. Evidence: `Saved/Runtime071-Stick-final.log`,
`Saved/StickHints.png` and `Saved/StickSettings.png`.

Both release archives passed CRC, version, architecture and native-binary hash
checks. All eight installed Windows files match the package by SHA-256.
Evidence: `Saved/Releases/BuildingSkills-0.7.1-verification.json` and
`Saved/Install071-final.log`. Tests used a fresh temporary world with saving
blocked. Local construction-message roundtrips do not establish online Linux
behavior; that remains a server test.

## 0.7.0: Stick spline placement

The editor suite passed all 16 tests. Stick geometry coverage includes straight
projection, signed slope and radius, nearest points on later helix turns,
continuous tangents, maximum length, half-turn subdivision, and subdivision
along the original native cubic curve. Evidence: `Saved/Automation070-results.json`.

The installed Windows candidate passed isolated construction checks for belts,
pipes, hypertubes and rails. Both straight three-piece paths and nine-piece,
multi-turn paths survived native construction-message roundtrips and produced
reciprocal connections at every internal join. Costs include all pieces. Native
first-endpoint transitions also retain the start and reach the requested camera
tip in Default or its constrained projection. These checks caught and corrected
native support-height offsets before release. Evidence:
`Saved/Runtime070-Stick-tip.log` (142 assertions, no failures).

A rendered run additionally checked physical Alt+S/K/wheel input, mouse focus,
typed step/percent/radius values, linked degrees, Apply and Escape. The settings
and hints were visually inspected. The final installed release passed **149
assertions**, zero failed assertions and a normal process exit; evidence is
`Saved/Runtime070-Stick-final.log`, `Saved/StickHints.png` and
`Saved/StickSettings.png`. Windows and Linux native compilation and packaging
succeeded. Both archives passed CRC, version, architecture and native-binary
hash checks; all eight installed Windows files match the package by SHA-256.
Archive evidence: `Saved/Releases/BuildingSkills-0.7.0-verification.json`.
These fixtures use fresh worlds with saving blocked. They do
not establish Linux online behavior, every native route variant, or support for
unrelated third-party spline implementations.

Run `scripts/RuntimeTest.ps1 -StickOnly -Render` for the dedicated regression.
See [STICK.md](STICK.md) for controls and the player test checklist.

## 0.6.5: Preview recipe context without a global lookup hook

The follow-up server log confirms 0.6.4 was installed and still crashed while
loading the same save, at the same relative address outside FactoryGame's load
segments. The array-return fix did not resolve this reported failure.

Blueprint preview pricing no longer hooks `AFGRecipeManager::Get(UWorld*)`.
During synchronous pricing on the game thread, only the native blueprint preview
world temporarily borrows the live world's GameState pointer. Native recipe
lookup then works normally. A scope guard restores the exact previous pointer;
no GameState events are broadcast, no level collections are changed, and the
live world and GameState actor are untouched. Unrelated worlds are rejected.
The existing scoped native refund and spline-cost handling is retained.

This removes the hook matching a reconstruction of the likely trampoline slot;
the log lacks a memory dump or map needed to confirm that allocation. Actual
Linux save-loading validation is still required. No save repair or migration is
included. Blueprint regressions additionally check nonempty member costs and
restoration of native preview/live recipe lookup after pricing.

Windows and Linux native builds and packages passed. Final Linux symbol
inspection confirms the recipe-manager hook machinery is absent and the 0.6.4
aggregate-return fix remains. Both archives passed CRC, metadata, architecture
and native-binary hash checks. All eight installed Windows files match the
package. The installed candidate passed **119 blueprint assertions** and **93
network assertions**, with zero failed assertions or fatal errors and normal
process exits. Neither runtime log registers the removed recipe lookup hook.
Network checks cover costs, original/copy graph separation, copied double-outlet
power, and pump/pipe validity after background simulation. Evidence:
`Saved/Runtime065-Blueprints-final.log`, `Saved/Runtime065-Networks-final.log`,
`Saved/Verification065-native-hooks.log`, and
`Saved/Releases/BuildingSkills-0.6.5-verification.json`. The tests used fresh worlds
and did not load or write player saves. Linux server save loading remains untested
locally; these Windows passes do not establish resolution of the reported crash.

## 0.6.4: Linux blueprint cost return convention

The blueprint deduplication cost hook added in 0.6.0 used SML's member
aggregate-return helper, which places the return buffer after the object pointer.
A Clang probe targeting Linux confirms that the native SysV ABI puts the return
buffer first. The mod now uses a typed Linux dispatcher while retaining SML's
shared before/after handler lists. Windows retains its existing hook path.
Evidence: `Saved/probe_return_abi.cpp` and `Saved/probe_return_abi.ll`.

Windows and Linux native builds and packages passed. Inspection of the final
Linux binary confirms the typed dispatcher and absence of the old cost-return
trampoline (`Saved/Linux064-cost-hook-assembly.txt`,
`Saved/Verification064-Linux-abi.log`). Both archives passed CRC, version,
architecture and native-binary SHA-256 checks. All eight installed Windows
files matched their archive. The installed Windows candidate passed **117
blueprint assertions**, zero failures or fatal errors, and exited normally:
`Saved/Runtime064-Blueprints-final.log`. This test used a fresh world, with no
player save loaded or written. It does not execute the Linux-only dispatcher.

This is a confirmed Linux hook defect, but its relationship to the reported
save-loading crash is not established. The supplied log has one unnamed frame.
The downloaded server binary matches three recorded hook addresses, but the
crash PC lies outside all of its load segments. Two cached neighboring-mod hook
addresses also place that module beyond the crash. Generated hook code is a
possibility; the existing log does not identify it conclusively. Evidence:
`Saved/ServerCrash064-symbolized.txt`. No save migration or existing world repair
is included. Linux runtime validation remains outstanding.

## 0.6.3: Dismantle Volume anchor interaction

The final installed Windows build passes **41 Volume assertions**. The existing
shared Alt+A mapping is exercised through actual input in Dismantle Volume, with
a naturally traced foundation target. Checks cover placing/clearing the anchor,
keeping the volume fixed when the player moves, returning to player-following,
and showing the configured hint. Volume messages now describe anchoring instead
of blueprint Save/Copy. Background scans preserve these messages, and anchor
changes invalidate the volume query cache.

Initial target assertions failed because another fixture building obstructed the
chosen foundation. The final fixture uses the unobstructed foundation and logs
the camera, bounds and native hit for verification. Evidence:
`Saved/Runtime063-Volume-final.log` and `Saved/DismantleAnchor063.png`.
Windows and Linux Server compile and package successfully; both archives pass
CRC, version, architecture and native-binary SHA-256 checks. All eight installed
Windows files match their archive. Linux gameplay was not tested. The isolated
Windows test did not load or write any player save.

## 0.6.2: Native deduplication hint and volume surfaces

A vanilla hint entry is attached to the native Build Gun keyboard list while a
blueprint hologram is active, using the configured key and native appearance
animation. The separate supplemental entry is removed. The final installed
Windows package passes **117 blueprint assertions**, including the configured
Alt+D cycle, scoped-context removal, and native hint visibility in both Alt+C
and saved-blueprint placement. Both screenshots were inspected and show the
readable shortcut. Mapping flags alone were insufficient; the earlier
geometry-only assertion also missed the entry's hidden animation state. The
final check includes opacity. Evidence: `Saved/Runtime062-Blueprints-final.log`,
`Saved/CopyHints.png` and `Saved/BlueprintHints.png`.

The installed volume candidate passes **32 Volume assertions**. Closed, two-sided
translucent sphere/cube surfaces use depth testing, the native color preference
and fixed opacity. Checks include the surface material, asymmetric cube bounds,
wheel resizing, rotation, selection and settings. Both shape screenshots were
inspected. Evidence: `Saved/Runtime062-Volume.log`, `Saved/VolumeSphere.png` and
`Saved/VolumeCube.png`.

Windows and Linux Server compile and package successfully. Both archives pass
CRC, version, architecture and native-binary SHA-256 verification; all eight
installed Windows files match the final archive. Linux is build/package validated,
not tested in a live dedicated-server session. No player save was loaded or written.

## 0.6.1: Volume input and settings

The final installed Windows package passes **30 Volume assertions** in a fresh
temporary world. The regression sends mouse-wheel key events through the input
system, rather than calling the gun's scroll method directly. It checks the
initial 8 m sphere radius and 16 m cube sides, rendered sphere dimensions,
resizing, opposite cube faces, rotation, foundations and ordinary actor buildings,
automatic preview before Ctrl, anchored preview and multi-selection, dimension
reset on re-entry, native color-picker registration and RGB value propagation.
The K popup contains only size and rotation step fields; mouse focus, typing,
Escape and context cleanup pass. The popup screenshot was inspected.

All **15 editor tests** passed. Windows and Linux binaries compiled and linked;
both archives passed CRC, version, architecture and native-binary SHA-256 checks.
All eight installed Windows files match its archive by SHA-256. The test exited
normally; it never loaded a user save and blocked saving its temporary world.
Linux was packaged, not exercised in a dedicated multiplayer session.

Evidence: `Saved/Runtime061-Volume-final.log`, `Saved/Automation/index.json`,
`Saved/VolumeSettings.png`, `Saved/InstallationVerification.json`, and
`Saved/Releases/BuildingSkills-0.6.1-verification.json`.

The earlier 0.6.0 scroll assertions below exercised the gun method, not the
mouse-wheel mapping; they did not establish working wheel input in gameplay.

## 0.6.0: Volume and blueprint deduplication

The installed Windows Volume fixture passes **17 assertions**, including center
inclusion, Edit and Dismantle modes, native scroll, Alt/opposite face, Shift rotation,
anchor position, persistent Ctrl selection, real mouse focus and typed numeric
input, Escape and input-context cleanup. Settings and translucent boundary
screenshots were inspected. Evidence: `Saved/Runtime060-Volume-final.log`,
`Saved/VolumeSettings.png` and `Saved/VolumePreview.png`.

The final archive changes pass **93 pipe/power-copy assertions** in a
fresh temporary world, including source isolation, reciprocal internal connections,
both sides of a copied double outlet and sustained pipe-network simulation. The
expanded fixture verifies matching preview/placed native costs, free full overlap
without duplicate pipes or cables, exact pricing for one missing pipe and stable
repeated cost queries. The final run uses NullRHI; earlier rendered runs cover the
original 87 checks. Evidence: `Saved/Runtime060-Networks-final.log` and
`Saved/Runtime060-Networks-cost-final.log`. This does not reproduce the previously
reported dedicated-server pressure crash or prove a Hover Pack flight scenario.

The final editor suite passes **15 tests**, with no failures or warnings. Coverage
includes sphere/box membership, individual face edits, opposite faces, rotated
axes, placement rounding, symmetric foundation rotations, partial overlaps and
different scales. Evidence: `Saved/Automation/index.json` and `Saved/Automation.log`.

The installed Windows blueprint fixture passes **114 assertions**, including
all/partial Skip, Replace, copied hierarchy, material variants, ingredient costs,
stocked storage and actual Alt+D mode/context transitions. Evidence:
`Saved/Runtime060-Blueprints-final.log`. Full overlap costs nothing; partial overlap
charges exactly for the missing member. The fixture also confirms source storage
contents stay unchanged. Earlier failed runs exposed preview-world event dispatch,
missing recipe-manager context and unlock-dependent material lookup. Cost reads now
use native virtual refund calculations with a scoped live recipe-manager fallback
for the exact preview world. Saved spline curves supply native arc lengths before
BeginPlay. Skip prices remaining members directly rather than subtracting from a
header that can include external source connections. Native array-return hooks
initialize the original return buffer before overriding it. Earlier crash logs are
retained as `Saved/Runtime060-Blueprints-nativecost-crash.log` and
`Saved/Runtime060-Networks-remainingcost-crash.log`.

Windows, editor and Linux native builds succeed. Both release archives pass CRC,
version, x64 architecture and binary SHA-256 checks; all eight installed Windows
files match the Windows archive. Evidence:
`Saved/Releases/BuildingSkills-0.6.0-verification.json` and
`Saved/InstallationVerification.json`. Linux is compiled and packaged, but an
actual Linux dedicated-server session has not been exercised locally.

Manual multiplayer checks must include partial and full blueprint overlap, material
variants, stored machine items, cables/pipes at skipped/replaced boundaries, native
blueprints and Alt+C copies, repeat placement, copied group hierarchy, rebindings,
and two players using different deduplication modes. Clients and server must use
0.6.0 because construction messages now carry the deduplication choice.

## 0.5.4: group snapshots and network copy isolation

The supplied dedicated-server log contains a confirmed oversized group-subsystem
replication message: 194,342 bytes against a 65,536-byte limit. Group metadata now
uses acknowledged fragments and is committed atomically on clients. Native 64-bit
actor identities preserve distinct members before their actors become relevant.
These IDs are transient; the saved group schema is unchanged. Editor coverage
includes a 2,048-member group, nested groups, empty snapshots, duplicate and missing
fragments, premature completion and saved-property compatibility.

The initial installed-game network run reproduced five failed assertions: copied
connections were not fully isolated or reciprocal, and copied networks remained
linked to source networks. The source connection lists stayed unchanged. The
reference-filter candidate passed all **75 network assertions**, including both
faces of a Double Power Outlet Mk.1 sharing a new circuit. Evidence:
`Saved/Runtime054-Networks-baseline.log` and
`Saved/Runtime054-Networks-filter.log`.

Adding an excluded pipe to the fixture exposed another fault: native placement
cleanup could disconnect the original pipe through a foreign reference on its copy.
The guard now clears such references on new components before native cleanup,
without calling reciprocal disconnection on the originals. It also refreshes
cached wire counts on copied power components. The guarded installed Windows run
passes **87 network assertions**, including unchanged source connections, separate
new circuits and pipe networks, both faces of the copied outlet remaining powered,
and thirty seconds of pump-and-water-pipe simulation. The producer is controlled
native circuit input; this is not an actual Hover Pack flight test.

The same candidate passes **90 blueprint assertions**, **94 Connected assertions**
and two transform-queue assertions, with no failures and normal exits. The two
existing Smart-menu checks remain skipped for the external K conflict described
below; no other mod or binding was changed. All fixtures use fresh temporary worlds
with saving blocked. They do not load or repair user saves. Evidence:
`Saved/Runtime054-Networks-pipe-boundary-fail.log`,
`Saved/Runtime054-Networks-final.log`, `Saved/Runtime054-Blueprints-final.log`
and `Saved/Runtime054-Connected-final.log`.

The final transport review also retains native actor identity tracking when an
actor leaves and later re-enters client relevance. Snapshot tests exercise chunk
assembly and identity handling; actual remote-client relevance and large-world
transfer still need dedicated-server validation. Client and server must both use
0.5.4 because the group replication protocol has changed.

FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully.
All **13 editor tests passed** after the final transport review, with no warnings
or failures (`Saved/Automation/index.json`). The gameplay logs above were recorded
before that final actor-relevance tracking adjustment; network-copy and blueprint
code was unchanged by it. Local authoritative fixtures do not exercise networked
snapshot delivery.

The final Windows and Linux archives passed CRC, version, architecture and native
binary SHA-256 checks. All eight installed Windows files match the final package.
Evidence: `Saved/Releases/BuildingSkills-0.5.4-verification.json` and
`Saved/Install054-complete.log`. The Linux package was built and verified but has
not been run on a dedicated server locally.

The reported server crash ends in native pipe pressure-group simulation with an
out-of-range index. This exact crash has not been reproduced or conclusively
attributed. Passing local copy tests does not establish multiplayer Hover Pack
behavior or repair of previously damaged networks. No migration or repair pass is
included; previously affected pipes/outlets are to be rebuilt manually.

## 0.5.3: grouping revisions and Connected hover performance

FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully.
All **12 editor tests passed** on the final build. The installed Windows package
passed **94 Connected gameplay assertions** and two transform-queue assertions,
with zero failures and a normal exit. Saving was blocked in the temporary world.
The two pre-existing Smart-menu checks remain skipped for the external K conflict
described below; no other mod or binding was changed.

New packaged regressions exercise explicit-building grouping with an intentionally
stale client revision on the authoritative local world, unchanged autosaves,
rejection of stale selected groups, actor/lightweight movement between spatial
regions, and reuse of shared material preview actors. Adding 128 distant
foundations left the local search at eight candidate checks across two queries.
This is a work-count regression, not an FPS measurement of a large multiplayer save.

The initial move-and-destroy constructor fixture exposed stale native proximity
registration. Moves now transfer that entry to the current location, and the
same fixture completes without crashing. The final run also passes existing
paint execution, preview cleanup, settings, radial, AltGr and gun-transition checks.
Material and swatch preview screenshots were visually inspected.

Evidence: `Saved/Connected053-Runtime-final.log`, `Saved/Connected053-*-proximity.log`,
`Saved/Automation/index.json` and `Saved/Connected053-Connected*PreviewTest.png`.
Both release archives passed CRC, version, architecture and native binary SHA-256
checks. All eight installed Windows files match the final package. See
`Saved/Releases/BuildingSkills-0.5.3-verification.json` and
`Saved/Connected053-Install.log`. Linux gameplay and actual remote-client latency
remain unverified locally.

Dedicated-server follow-up: group two individual buildings after an autosave or
another player's unrelated edit. Sweep the Customizer across a large connected
factory with a new swatch and material, checking both responsiveness and preview
membership. Apply once and verify the same members change on both clients.
Update the server for the grouping fix and clients for the hover optimization.

## 0.5.2: native radials, regex names and AltGr input

FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully.
All **12 editor tests passed** on the final build (`Saved/Automation/index.json`).
The installed Windows package passed **82 Connected gameplay assertions** and
two transform-queue assertions, with
no failed assertions and a normal exit. The temporary test world blocks saving.
Two Smart-menu checks remain explicitly skipped for the external K conflict
recorded below; no other mod or binding was changed.

The packaged checks cover tap R cycling exactly once, holding R in Dismantle,
Edit, Transform and spread painting, all ten Transform choices, mouse selection
and release of Scale XYZ and Connected spread, retained Edit selection, reopening
Connected settings after the radial closes, and AltGr+7 entering `|` in the
whitelist without switching the gun. Existing paint preview, material replacement,
selection, settings and gun-transition regressions also pass. Custom hints are
hidden while a native radial is open to avoid overlapping controls.

Native input can assign the first supported descriptor after a short release.
The local-tool handler ignores that assignment and accepts one deferred commit
after a held radial closes, preserving the replicated selection descriptor.
Editor coverage verifies that regex uses readable building names even without a
recipe and that `(?i)(?:belt|pipe)` excludes Foundry, power poles, power lines and
resource well extractors. Character classes retain standard ICU semantics.

Evidence: `Saved/Radial052-Runtime-final.log`,
`Saved/Radial052-*-deferred.log` and the visually inspected
`Saved/Radial052-{TransformRadial,SpreadRadial,RegexInput}-final.png` captures.
Windows and Linux release archives passed CRC, version, architecture and native
binary SHA-256 checks; all eight installed Windows files match the package.
Evidence: `Saved/Releases/BuildingSkills-0.5.2-verification.json` and
`Saved/Radial052-Install-deferred.log`. Gameplay validation was local Windows;
the Linux package has not been exercised on a dedicated server.

## 0.5.1: controls across gun-state transitions

Leaving Connected Dismantle or painting now detaches the hint container before
removing its display panel. Re-entering attaches the current hints even when
their text has not changed. Supplemental Dismantle and Customizer hints use a
separate bottom row above the native hints; Edit retains its replacement row.

The expanded Connected regression checks hint attachment across Build, Dismantle,
Customizer, unequip and Transform transitions. It also checks the effective K
mappings and routes K through Slate with a foundation hologram active, so stale
modal focus cannot be hidden by direct PlayerInput injection. These checks run
in a temporary world with world saving blocked.

Spread painting now labels R as Toggle spread and uses the native below-cursor
mode row. The default search limit is 64 objects. Its regression includes
prospective swatch/material previews and outlines, unchanged stored customization
before applying, a nonuniformly scaled foundation, and cleanup on target changes,
opening settings, changing modes and leaving the gun. It also moves a previewed
lightweight foundation through the native transform path and verifies that
cleanup releases the temporary actor without changing stored customization.

The original K failure was reproduced with the installed mod set: Auto Rail
Supports' consuming K action at priority 1000 shadowed Smart's K action at priority
100, while no Building Skills K mapping remained and keyboard focus was on the
game viewport. Other mods and their bindings are unchanged. The interoperability
check reports an explicit skip when that external conflict remains; it does not
claim that Smart opened successfully. Diagnostic evidence:
`Saved/Shortcuts051-Runtime-first.log`.

FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully.
All **12 editor tests passed**. The installed Windows package passed **72 Connected
gameplay assertions** and two queue assertions, with no failed assertions and a
normal exit. Two Smart-menu checks were explicitly skipped for the external K
conflict described above. Evidence: `Saved/Automation/index.json`,
`Saved/Spread051-*-release.log` and `Saved/Spread051-Runtime-final.log`.

The Connected and Transform windows and both prospective paint previews were
visually inspected. Both paint previews retain the native Spread caption and
show the two affected foundations, including the scaled one. Evidence:
`Saved/Spread051-Settled-final.png`, `Saved/Spread051-TransformSettings-final.png`,
`Saved/Spread051-SwatchPreview-final.png` and
`Saved/Spread051-MaterialPreview-final.png`.

Windows and Linux archives passed CRC, version, architecture and native-binary
SHA-256 checks. All eight installed Windows files match the package. Evidence:
`Saved/Releases/BuildingSkills-0.5.1-verification.json` and
`Saved/Spread051-Install-final.log`. These gameplay checks ran locally; the Linux
package has not been run on a dedicated server.

## 0.5.0: Connected Components

FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully.
All **12 editor tests passed**, including connected geometry and filters: touching
faces/edges/corners, vertical neighbors, rounding tolerance, meter conversion,
single-box gap expansion, capped traversal, regex validation and a 10,000-box chain.
Evidence: `Saved/Automation/index.json`.

The installed Windows package passed **29/29 Connected gameplay assertions** in
a fresh isolated world with saving blocked. These exercise lightweight foundations,
gap/count/name/category filters, Edit and Dismantle Connected modes, persistent
selection, K, mouse clicks and typing in the settings window, Apply and input
restoration, R painting modes, native gun clicks, connected swatch application and
material replacement while leaving disconnected foundations unchanged. The final
window screenshot was visually inspected after the native startup splash ended.
Evidence: `Saved/Connected-UI-Runtime.log` and
`Saved/ConnectedSettingsSettled.png`.

The existing blueprint/Transform regression also passed **90/90 assertions**,
including K for temporary Transform settings and its mouse/input behavior.
Evidence: `Saved/Connected-BlueprintRegression.log`. Both runtime runs exited
normally. These are local packaged gameplay checks; they do not establish remote
multiplayer behavior. The Linux server package is built and archive-verified,
but has not been run on a dedicated server.

Both release archives are checked for CRC, version, architecture and native-binary
SHA-256 equality. All eight installed Windows files match the final archive.
Evidence: `Saved/Releases/BuildingSkills-0.5.0-verification.json` and
`Saved/Connected-UI-Install.log`.

Manual multiplayer acceptance: cycle to Connected in Edit and Dismantle, change
the shared settings with K, accumulate connected selections with Ctrl, and check
that switching modes retains them. In the Customizer, use R for Connected and
apply a swatch/material to touching foundations, then test a deliberate gap and
material/swatch boundaries. Verify costs and resulting changes from a second
client. Rebind K and confirm normal K behavior outside these contexts.

## 0.4.9: step-window mouse input

The step window explicitly applies the native interact-widget input mode after
being pushed onto the UI stack. Its focus target is the window itself, mouse
locking is disabled, and clicks keep the cursor visible. Closing it uses the
native stack's restoration of gun input.

The previous 0.4.8 regression forced keyboard focus into a numeric field; it did
not establish mouse usability. The revised packaged regression checks cursor
visibility, released permanent mouse capture, blocked camera input, mouse focus
of the numeric field, clicking Close, reopening, Escape, and restoration of camera
input. It routes pointer events through Slate instead of assigning field focus.
FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully;
all 10 editor tests passed. Both archives passed CRC, version, architecture and
native-binary SHA-256 checks. Evidence: `Saved/Mouse049-Tests-final.json`,
`Saved/Mouse049-*.log` and `Saved/Releases/BuildingSkills-0.4.9-verification.json`.

The installed Windows package passed **90/90 focused gameplay assertions** and
exited normally. Mouse clicks focused the numeric field and activated Close;
typing 37.5 applied that movement distance to a building. Cursor visibility,
released mouse capture and suspended camera input were checked while open;
closing restored camera input and hid the cursor. Reopening and pressing Escape
before clicking a field also returned to Transform. Both the window's leaf-key
handler and native pause route consume Escape instead of passing it to the gun.
Closing Edit still restores configured step values. Evidence:
`Saved/Mouse049-Runtime-final.log`. All seven installed Windows files match the
final archive by SHA-256. The Linux server archive is built and verified; no
dedicated-server gameplay test was performed for this UI fix.

## 0.4.8: modifier shortcuts, blueprint anchors and temporary steps

Save/Copy use configurable Alt+S/Alt+C; Alt+A selects or clears a separate blueprint
anchor. Plain S/C/A retain movement. The anchor is excluded unless selected and
uses blue/purple highlighting for excluded/included status. All three anchor axes
orient Save/Copy bounds. Its lower geometry can extend the floor without changing
the selected X/Y bounds or adding a member to the archive. With no anchor, the
previous automatic majority alignment is unchanged.

The installed Windows package passed **83 focused gameplay assertions**. These
cover external/included anchor membership in Save and Copy, Alt+A selection and
clearing, native hologram floor spacing, foundation snapping that retains that gap,
and Alt+C handoff. Blue/purple anchor screenshots were visually inspected.

The final full rendered regression passed **185/185 assertions** with a normal
exit, covering grouping, selection modes, dismantle/Edit highlights, transforms,
connected networks, Save/Copy and the temporary step window together. It used a
fresh isolated world with saving blocked. Evidence: `Saved/Steps048-Full-final.log`.
These local checks do not establish remote multiplayer behavior.

Transform's configurable M window uses the native UI stack without unequipping the
gun. The packaged regression types 37.5 into its real distance field, moves a placed
building by 37.5 cm, closes the window through an Escape key event, returns to Edit,
and checks that only closing Edit resets temporary values to configuration. The
rendered menu was inspected. Normal, Tiny, Small and Large each expose distance,
rotation and scale fields. Evidence: `Saved/Steps048-Blueprints-final.log`,
`Saved/StepSettingsTest.png`, `Saved/AnchorBlueTest.png` and `Saved/AnchorTest.png`.

FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully.
All **10 editor tests passed** with regenerated inputs, including plain A/S/C
movement, Alt chords, configurable M, complete anchor orientation, and lower/higher
anchor floor calculations. Evidence: `Saved/Steps048-Tests-final.json` and
`Saved/Steps048-*-final.log`. The Windows and Linux archives passed CRC, version,
architecture and native-binary SHA-256 checks; all seven installed Windows files
match its archive. The Linux package has not been run on a dedicated server.

Manual acceptance: set a lower, unselected foundation as anchor, save/copy another
selection, and confirm the resulting floor and axes. Ctrl-select the anchor and
check it turns purple and becomes part of the content. Clear it with Alt+A and
confirm automatic alignment returns. Repeat with a tilted/scaled anchor, a
lightweight foundation, and an ordinary machine; test Save/Copy on the Linux server.

## 0.4.7: selection Save, Copy and pooled machine covers

Save now accepts nonempty individual, group, mixed and blueprint selections.
Copy defaults to configurable C, creates a native placement hologram, and retains
selected group nesting with new identities. The source selection is unchanged.
The clipboard is transient and is not entered in the blueprint library.

The editor input regression now checks both S and C, including suppression of
backward movement/crouch while the selection context is active and restoration
after it is removed. All 10 editor tests passed with the regenerated input assets.
The isolated packaged regression now includes Save without groups, mixed-selection
deduplication, clipboard encode/decode, native hologram contents/construction,
fresh parent/child group identities, and unchanged original buildings. The installed
Windows build passed all **67 focused blueprint assertions**, including the actual
C-key handoff to the native Build Gun. Clipboard creation leaves no blueprint files
or library entry. The native designer screenshot was inspected.

The final full rendered regression passed **170/170 assertions** and exited
normally. The rebuilt editor also passed **10/10 tests** with no warnings or
failures. These local results do not establish dedicated-server gameplay behavior.

Manual online coverage still needed: copy a single building, several foundations,
two groups plus a loose machine, and a nested group; place each twice and verify
that selecting or editing one copy cannot affect the source or the other copy.
Check normal material costs, cancellation, simultaneous players, custom scales,
and Save from a selection without first creating a group.

### Pooled machine covers

Reported on a multiplayer constructor: black conveyor-port covers remained at
the old position after moving the machine. The fix refreshes the native
pooled decorations when an actor transform changes, on rendering peers only.
It uses the pool's queued removal/registration APIs rather than directly editing
background-thread-owned transforms. Repeated updates while registration is pending
do not enqueue another registration.

The opt-in `scripts/RuntimeTest.ps1 -Render -PooledVisualsOnly` regression checks
visible native fog-plane meshes after translation, rotation, nonuniform scaling,
restoration, rapid repeated transforms and replay of an unchanged pose. It checks
for misplaced visible planes and duplicate handle sets after the pool settles.
The rendered installed-build test passed **49/49 assertions**: all four visible
cover meshes matched the expected pose in every phase, with no misplaced planes
or duplicate handles. This does not exercise a remote client. Multiplayer still
requires a constructor test on the dedicated server,
including undo/redo, moving out of range and back, and reconnecting.

Editor, Windows client and Linux server native compilation succeeded. Runtime
tests use fresh temporary worlds with world saving blocked. The initial cover
fixture crossed the native pool's distance-culling threshold; the corrected
fixture keeps all tested poses in range and verifies actual visible components.

Installation testing also exposed deferred virtual-hook registration, missing
customization in synthetic foundation fixtures, and a native Build Gun state
handoff that overwrote the prepared descriptor. These were corrected and rerun.
Copy now checks recipe availability, primes the native build-state descriptor,
and suppresses the archive writer's disk side effect only for transient clipboards.

Both platform packages were produced successfully. The versioned archives
under `Saved/Releases` passed ZIP integrity, descriptor version, binary architecture
and fresh-binary SHA-256 checks. Both cooked content containers also include the
new `IA_Copy` asset and updated `MC_Save` mapping. This verification covers the
final selection Save/Copy implementation as well as the pooled-cover changes. All seven
installed Windows files match the final 0.4.7 package by SHA-256. The previous
0.4.6 installation was backed up. Linux-server execution remains unverified.

Evidence: `Saved/Installed047-Full.log`, `Saved/Installed047-Blueprints.log`,
`Saved/Installed047-Pooled.log`, `Saved/Installed047-EditorTests.json`,
`Saved/Releases/BuildingSkills-0.4.7-verification.json`, and
`Saved/InstallationVerification.json`. Earlier failed runs are retained separately.

## Version 0.4.6 verification

Restores native dismantle highlighting instead of forcing the generic red outline.
The extra hover layer after a pinned Ctrl-selection uses the native dismantle
style too. Edit retains its green palette. The runtime regression now checks
native dismantle before selection and compares pinned and subsequent hover targets.

The 0.4.5 outline assertion incorrectly required generic red, so its passing result
did not verify the intended native appearance. The multiplayer transform rejection
was also reported again after 0.4.5; the earlier simulated spawn-rounding test did
not establish that the real client/server problem was resolved. The supplied server
log shows version zero on both sides and a 0.050 cm X-position difference.

The new native-serializer regression reproduced a failure at X=-247199.75 cm
against the 0.4.5 rounding calculation. The correction calls Unreal's packed-vector
quantization helper, whose negative ties round away from zero. Neighboring values
and positive ties are checked too; per-building stale-edit versions remain enforced.

FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully.
The corrected editor run passed **10/10 tests**, including the midpoint case that
failed before the correction. The final rendered packaged run passed **155/155
assertions** and exited normally. It confirmed the exact constructor midpoint
fixture, acceptance of its network-rounded position, rejection of stale edits,
native dismantle styling before selection and after Ctrl, and unchanged green Edit
highlighting. The dismantle screenshot was visually inspected. The test used a
fresh temporary world with world saving blocked. Actual Linux-server execution and
a remote multiplayer retest remain unverified.

The first packaged run exposed three failures in the newly added test fixtures:
directly relocating a static constructor left it at its original position, and the
new hover check reused two actors destroyed by the earlier dismantle check. The
fixtures now use the native move helper and fresh actors, with explicit setup
assertions. The failing run is retained in `Saved/Bugfix046-Runtime-first.log`.

Evidence: `Saved/Bugfix046-Automation-baseline.json` retains the reproduced failure;
`Saved/Bugfix046-Automation-result.json` retains the corrected pass. Final build logs are
`Saved/Bugfix046-Editor-final.log`, `Saved/Bugfix046-WindowsNative-final.log`, and
`Saved/Bugfix046-LinuxNative-final.log`. Both archives passed CRC, descriptor and
fresh native-binary SHA-256 checks; all seven installed Windows files match the
final package. The previous installation was backed up. The receipts are
`Saved/Releases/BuildingSkills-0.4.6-verification.json` and
`Saved/InstallationVerification.json`.
Gameplay evidence: `Saved/Bugfix046-Runtime-final.log` and `Saved/DismantleTest.png`.

For multiplayer acceptance, install 0.4.6 on both client and Linux server, then
retry the previously rejected constructor with and without a belt. In dismantle,
pin one building with Ctrl and hover another: both must retain the native highlight
appearance. Switching back to Edit must restore its green appearance.

### Reported Linux crash after blueprint creation

The supplied `FactoryGame-backup-2026.10.01-12.28.09.log` reports Building Skills
0.4.6 and Smart! 34.3.1. On October 1 (UTC), blueprint serialization validated and
completed at 12:25:44, world save disk output completed at 12:27:49, and the server
crashed at 12:28:09 with SIGSEGV. The active stack is Smart!'s deferred
`PurgeZombieChainActors` timer calling native `RemoveConveyor`,
`RevertChainActor_Unsafe`, and `MoveItemsFromChainToBelts`; the failing operation is
resizing a conveyor item array. Building Skills has no frame in that active stack,
which does not exclude a preceding transform or blueprint interaction.

The matching Smart! 34.3.1 source was inspected under its source-available
license's view-and-study grant (commit `e5e59d3e1b662f3a28528489332c5cf6f7434730`).
Its deferred sweep can be scheduled after server construction and upgrades; this
does not identify the user's exact triggering action. No Smart implementation was
copied or modified. Root-cause isolation and a reproducible compatibility fix remain
pending; the local 155-assertion pass does not cover this reported server failure.

## Version 0.4.5 verification

Addresses native actor spawn quantization in multiplayer transform validation,
uses a per-building edit version to reject stale selections, moves blueprint Z=0
to the lowest point while keeping X/Y centered, and separates red dismantle from
green Edit outlines. Blueprint selection also accepts a native proxy target.

FactoryEditor, Windows Shipping and Linux Server Shipping compiled. Windows and
Linux archives passed CRC, descriptor and native-binary SHA-256 verification; all
seven installed Windows files match the candidate archive. The previous installed
version was backed up before deployment. Linux execution and a real multiplayer
client/server session were **not** exercised by this local validation.

The rendered, packaged, fresh-world regression passed **150/150 assertions**. It
used no existing save and blocked world saving. New checks cover a constructor
with a spawn-rounded client transform, stale requests after a sub-rounding edit,
connected-belt movement, red dismantle and green Edit outlines, full foundation
mesh enclosure, native blueprint construction/membership, proxy targeting, and
foundation targeting in Blueprint mode.

The first two candidates failed the new enclosure/snapping assertions:
clearance omitted a foundation lip, and native center snapping placed the contained
foundations half a tile off-grid. The corrected two-foundation box is
800 x 1600 x 407.095 cm, centered on X/Y with its bottom at Z=0. Side snapping
places the foundations at local (800, 0, 0) and (800, 800, 0) relative to the target;
top snapping also passes its grid and 400 cm stacking-height check. Native clearance
padding is retained where needed. Ramps and incompatible foundation sizes retain
the native snapping path.

Evidence: `Saved/Bugfix045-Runtime-final.log`, `Saved/Bugfix045-Editor-final.log`,
`Saved/Bugfix045-WindowsNative.log`, `Saved/Bugfix045-LinuxNative.log`, platform
package logs, `Saved/Releases/BuildingSkills-0.4.5-verification.json`, and
`Saved/InstallationVerification.json`. The final editor automation run passed
**10/10 tests** with no failed tests, including native spawn quantization and
bottom-center blueprint alignment; its report is retained in
`Saved/Bugfix045-Automation-result.json`.

Targeted acceptance checks:

- On a dedicated server, move a constructor both without a belt and connected to
  an unselected belt. Check that the belt stretches, stays targetable and transports
  items. An older selection must still reject an intervening edit by another player.
- Save two adjacent foundations, place the new blueprint against existing
  foundations, and inspect its full bounds, floor origin, rotation and snapping.
  Re-save old centered-Z blueprints before testing the new origin behavior.
- Target either newly placed foundation in Blueprint selection mode. Both pieces
  must highlight and select together, in Edit and dismantle, including after reconnect.
- Hover an unselected building after pinning the first selection: dismantle remains
  red; Edit remains green. Repeat with blueprint and group selections and switch
  tools to ensure the previous color is restored.

## Version 0.4.4 verification

The [feature and provenance review](INSPIRATION.md) covers comparable mods, checked
licenses, implementation findings and the resulting changes. This version indexes
group traversal/parent validation and removes cancelled queued transforms before
submission. Editor regressions cover 2048 levels, stable member order, malformed
cycles, missing parents, duplicate membership, cancellation across all ten modes,
and preservation of the server's movement batch limit. The packaged harness also
checks the actual Edit component's queue for opposite movement and reciprocal scale.

On October 1, FactoryEditor, Windows Shipping and Linux Server Shipping compiled
successfully. All nine editor tests passed with zero warnings/failures; the report
is `Saved/FeatureReview-Automation-result.json`. The 2048-level regression completed
in approximately 0.47 seconds in the editor (a functional test duration, not a
gameplay frame-rate benchmark).

Both packages passed archive CRC, descriptor version, native-binary SHA-256 and
platform-format checks. Versioned Windows and Linux server archives and the receipt
are under `Saved/Releases`. The Windows installation was backed up before updating;
all seven installed files match 0.4.4 by SHA-256.

The full rendered packaged regression passed all 137 assertions and exited
normally. It covered selection persistence, independent hover, nested grouping,
native dismantle, transforms, undo/redo, network geometry/targeting, blueprint
Save and scale preservation, plus both new component-queue assertions. Its log is
`Saved/FeatureReview-Runtime-final.log`. The fresh test world blocked world saving;
no existing user save was loaded. Satisfactory was closed when validation finished.
Linux runtime execution and a real remote multiplayer session remain unverified;
local RPC/transport assertions do not establish remote delivery or late-join behavior.

## Version 0.4.3 verification

Adds upright blueprint alignment by the dominant building orientation, tight bounds
in that frame, and a three-axis bounding-box center. Includes the pending 0.4.2 Linux
serialization fix. New editor coverage checks a rotated/scaled foundation row,
geometric centering, quarter-turn equivalence, selection-order independence, minimum
area when alignment counts tie, majority priority, mirrored scale, and empty geometry.
The packaged group fixture now uses a 30-degree foundation row and checks both the
chosen frame and centering. FactoryEditor, Windows Shipping and Linux Server Shipping
compiled successfully on September 30. All seven editor tests passed, with zero
test warnings or failures, including `Blueprints.CenterAndDominantAlignment`.
The report is preserved as `Saved/BlueprintBounds-Automation-result.json`.
Both shipping packages completed and passed CRC checks; packaged native binaries
match the fresh builds by SHA-256, including the Linux x86-64 ELF library. The
versioned archives and verification receipt are in `Saved/Releases`.

Version 0.4.3 was installed on October 1 after backing up the previous installation.
All seven installed files match the release archive by SHA-256. The rendered,
focused blueprint regression passed all 37 assertions and exited normally. It
verified the native Save window, archive validation (`expected=3 serialized=3`),
successful disk write/readback, the exact selected contents, upright 30-degree
alignment, three-axis centering, unchanged source buildings, scale preservation in
headers/preview/hologram/world placement, and native lightweight conversion. It also
checked malformed-TOC rejection, native-loader compatibility, Save request recovery,
and transport snapshot data. The successful log is preserved as
`Saved/BlueprintBounds-Runtime-final.log`.

Early attempts stopped during Steam/EOS initialization before any assertions ran.
Main-thread stack snapshots showed the native menu waiting inside Steam client
initialization; Steam was not running. Starting Steam allowed the rendered regression
to finish. The harness now checks that Steam is running, disables EOS overlays with
process-local config overrides, and accepts an optional `TimeoutMinutes` parameter
for cold startup. No persistent overlay settings were changed.

The test used a fresh temporary world with world saving blocked. No player save was
loaded or modified, and the game was left closed. Actual Linux execution, online
blueprint file transfer, remote placement and late-join delivery remain unverified;
local RPC and transport assertions do not replace those online checks.

## Version 0.4.2 candidate verification

The reported Linux 0.4.1 log showed two buildings written by the native serializer,
but zero observed by the mod's header hook. This caused the new validation to reject
the save. The SDK defines that serializer inline, so observing calls to its exported
copy is not reliable across shipping compilers. Version 0.4.2 removes that dependency
for both scale preservation and group validation: they inspect the completed native
object table before writing to disk. Loading reads each header at the native object
loader boundary and rewinds before normal processing.

The focused regression now checks the actual stored headers without installing an
observer on the inline serializer, and checks rejection of trailing unparsed TOC data.
FactoryEditor, Windows Shipping and Linux Server Shipping compiled successfully on
September 30. Both shipping packages completed and passed CRC integrity checks.
Their native binaries match the fresh builds by SHA-256, including the Linux x86-64
ELF library. Versioned archives and a verification receipt are in `Saved/Releases`.
Linux packaging was rerun after the simultaneous Windows packaging job finished,
because Unreal AutomationTool permits only one packaging process at a time.

This candidate was not installed while the user was playing. Its revised archive
regression subsequently passed as part of 0.4.3 on October 1; no separate 0.4.2
gameplay pass is claimed. Linux online saving and scale placement still require
verification on the actual server.

## Version 0.4.1 verification

The Windows results below did not cover the Linux inline-serializer failure reported
after release. Use 0.4.2 or later for testing its replacement.

Adds native blueprint scale preservation and corrects the group Save origin. The
reported multiplayer blueprint was inspected read-only: it contained a concrete ramp
and foundation, with origins approximately 18 and 20 metres below the blueprint
origin and offset sideways. It was populated, but incorrectly positioned. The
transient designer now supplies the correct native transform instead of depending
on a hook of the small native origin getter.

The focused packaged blueprint run passed all 34 assertions on September 30,
including selection-relative group coordinates, a scaled
lightweight member saved through the group window, uniform/nonuniform native header
round trips, preview-world transforms, native hologram scales, rotated world placement,
actor replication snapshots, unmodified-loader compatibility, and unchanged ordinary
world-save header serialization. The placement assertion checks the native lightweight
instance after foundation actors convert to instances. Compatibility was checked by
bypassing the mod's scale-loading scope for the same file; this was not a separate
mod-free game launch. Replication assertions check transport data locally, not delivery
to a remote client. The run used a fresh temporary world with world saving blocked;
no player save was loaded or modified. The successful log is retained as
`Saved/BlueprintScale-Runtime-final.log`.

Windows Shipping and Linux Server Shipping compiled and packaged successfully.
Both archives passed CRC checks, and their native binaries match the fresh builds
by SHA-256; the Linux library is x86-64 ELF. Versioned packages and the verification
receipt are in `Saved/Releases`. Version 0.4.1 is installed on the Windows game:
all seven installed files match the release package by SHA-256. The previous
installation was backed up. The test exited normally and left the game closed.

Online verification must include two foundations saved on the Linux server, download
to the client, preview at the placement origin, placement, and late join. Existing
incorrectly positioned blueprints need to be saved again from their original group.

## Version 0.4.0 verification

The implementation adds configurable pivots/axis frames, reversible shear-free
stretching, transform undo/redo, combined input with visual previews, indexed network
planning, arc-length spline weighting, per-selection conflict checks, and bounded
late-join snapshots. It fixes the client Save hook cancelling native RPC forwarding
and restores independent hover outlines after pinning a selection.

New editor coverage exercises world/local frames, inverse operations on oblique
buildings and uneven spline weighting. New packaged regressions exercise undo/redo
of stretched connections, stale versus unrelated selections, hover after pinning,
client Save forwarding, actual blueprint disk-write reporting, membership retention,
and recovery from superseded transform replies.
FactoryEditor Win64 Development compiled successfully on September 30. All six
editor automation tests passed with zero test warnings and failures, including the
new world/local frame and spline-weighting test. Input assets were regenerated and
the input test verified all additional configurable controls. Steam Windows Shipping
and Linux Server Shipping both compiled and packaged successfully. Both archives
passed CRC integrity checks; their native binaries match the fresh builds by SHA-256.
The Linux library is x86-64 ELF. Versioned archives and the verification receipt are
in `Saved/Releases`.

Version 0.4.0 was installed on September 30 after the game was closed. All seven
installed files match the verified release archive by SHA-256, and the previous
installation was backed up. Packaged runs verified the Edit controls, persistent
hover outlines, default bounds pivot, rotation/scaling, blueprint Save forwarding,
actual disk write/readback, and timeout recovery. The first targeting assertions
incorrectly required a particular collision component and ran before native network
relevance activated fixture collision. The fixtures now invoke that native activation
callback and resolve both direct and shared-instance trace hits.

The corrected focused network run passed all 51 assertions after a collision cleanup
fix. It verifies baseline targeting, repeated belt/pipe/rail transforms, unique
collision boxes/handles, connected-machine movement, full-geometry undo/redo,
received-geometry replay, conflict checks, item/fluids retention and conveyor-chain
rebuilding. Collision rebuilding now clears the previous body setup's box definitions
before native registration; this prevents stale and duplicate shapes from accumulating.
The earlier full run's non-network checks passed; no fresh full-suite pass is claimed
after this focused fix. Both runs used temporary worlds with world saving blocked.
The captured Transform hints and Save-only blueprint window were visually inspected.

Remaining online checks require a Linux server and two Windows clients: simultaneous
independent/overlapping edits, undo conflicts, late join while transforming, client
blueprint file transfer, and targeting after repeated belt/pipe/rail transforms.
Actual save/reload and visual inspection of tight bends remain necessary.

## Earlier checks (0.3.x)

- FactoryEditor Win64 Development compiled and linked successfully.
- Input assets and the shortcut widget-hook registration were generated with zero errors.
- All five editor automation tests passed without test warnings or failures:
  `Groups.NestMergeUngroup`, `Groups.MergePreservesChildren`,
  `Groups.MetadataSaveRoundTrip`, `Input.EditChord`, and `Transform.PivotAndInverse`
  under `BuildingSkills`.
- Steam Win64 Shipping packaging and installation succeeded. All seven installed
  files matched the archive by SHA-256. `Install` now performs this verification
  automatically and records `Saved/InstallationVerification.json`.
- Linux Server Shipping compiled and packaged successfully. The archive's shared
  library matches the fresh build by SHA-256 and is an x86-64 ELF binary. This is
  build verification; Linux execution and online gameplay remain manual checks.
- Version 0.3.0 passed all 95 runtime assertions in a fresh temporary
  world. The test injects key events into the game's player input and uses the native
  mode-cycle API. It verifies Alt+E opening/closing, visible Edit HUD, building
  targeting, Ctrl multi-selection, selection persistence through Edit's Normal,
  Blueprint and Group modes, G grouping through the server request, M merging a
  single into a group, U ungrouping, and Edit's dismantle protection.
- The same run opens vanilla dismantle with F, selects multiple buildings, cycles
  its available modes, holds the dismantle input, and verifies that both selected
  buildings disappear while the unselected building survives.
- The group-hover fixture adds 128 native lightweight foundations to an actor-backed
  machine. It checks group expansion when aiming at the machine and at a foundation,
  stable selection/refund refresh counters, and bounded UI-widget counts. Stable
  hover recorded zero selection rebuilds, zero refund refreshes and zero new UI
  widgets. Vanilla dismantle is also checked against the 129-building group.
- Lightweight lookup checks add an identical overlapping foundation, remove the
  overlap, delete the original, and add a replacement elsewhere. Ambiguous matches
  are rejected and reused slots never become the original building.
- The rendered screenshot was inspected: the EDIT status panel is on the left,
  configured controls use native hint widgets at the bottom, and Alt+E / Edit sits
  inside the bottom-right shortcut grid. The native dismantle-only overlay is hidden
  during Edit and restored for dismantling.

- Transform checks enter with T, inject the native R and mouse-wheel bindings,
  and rotate a group containing one storage container and two lightweight foundations.
  All ten modes and their inverse steps restore the original native transforms.
  The fourth mode is uniform Scale XYZ; editor checks verify it scales all axes
  and distance from the pivot without changing rotation.
  Invalid scaling is rejected without changing the hierarchy revision.
- Held Ctrl plus Up moves that group along the camera-relative axis with the Tiny
  step. Page Up moves along Z. The native Escape route restores normal Edit, keeps
  the transformed selection, and does not open the pause menu.
- The transform screenshot was inspected for the left status panel, active-axis
  ring at the selection pivot, and replacement bottom hints. Modifier defaults were
  checked against the installed reference: Tiny = Left Ctrl, Small = Left Alt,
  Large = Left Shift. Configuration registration and the generated settings file
  were confirmed in the packaged game.
- Network fixtures exercise native belts, pipes and rails through all ten transform
  modes and inverse steps, including saved spline tangents, endpoint frames, visual
  instances and collision data. Reconstructing actors from transformed native spline
  data is checked separately; this is not an actual save/reload test.
- Boundary checks move a connected storage container, deform a selected belt between
  two fixed containers, stretch a connected lift, and retain pipe and rail links.
  Differently rotated pipes retain a shared endpoint when scaled together. Pipe
  contents remain intact, and a transformed miner keeps its original resource node.
- Power-tower checks use the native two-strand connectors. Both strands follow a
  translated and rotated tower, keep their far endpoints fixed, and update their
  mesh transforms along with saved endpoint data.
- A live conveyor chain is retired and rebuilt after rotation and scaling. All three
  test items remain present, and the rebuilt simulation path matches the belt spline.
- Blueprint checks open the native window with S, verify Load/Clear are hidden and
  Clear is blocked, and restore Edit with the same group when closing. The native
  save RPC writes a uniquely named blueprint which reads back and loads exactly one
  actor-backed building plus two lightweight foundations. Original buildings and
  transforms remain intact. The test deletes its unique blueprint afterward.
- The unattended blueprint test calls the native save RPC after the UI's online
  name-filter stage, which does not complete in that test environment. Interactive
  name filtering, clicking Save, overwrite confirmation and multiplayer file
  transfer still need manual coverage; the production UI keeps those native flows.
- Version 0.3.1 passed all 11 focused blueprint assertions using three lightweight
  foundations and no actor-backed machine. Before the fix, S reached the save code
  but empty actor-component bounds incorrectly rejected the group as out of range.
  The fixed run opens the window with S, verifies 1-by-3-by-1 blueprint dimensions,
  saves and reloads all three members, leaves originals unchanged, and returns to Edit.
  Lightweight geometry bounds now also contribute to mixed-group blueprint dimensions.

The reported multiplayer failure reached the Save handler with one selected group,
but every server reply carried a null save-session reference. The session inherited
`DORM_Initial` from buildables and the UI relied on an actor pointer arriving on a
different channel from its spawn. Version 0.3.2 keeps the session awake, waits for
its replicated request identifier, retains Save input while waiting, and cancels
timed-out requests by identifier. Remote Linux gameplay remains a manual check.
The 0.3.2 editor suite passed all five tests. Its packaged regression additionally
checks delayed session discovery, holding Save during the delay, stale replies,
awake replication settings and timeout cleanup. That new runtime scenario still
needs execution; installation is waiting for the running client to close.

Version 0.3.3 restores spline collision registration after rebuilding belts, pipes
and rails. The native collision teardown unregisters the component; merely calling
its physics creation method leaves it absent from scene queries. Registration now
recreates physics once through the native registration callback. New runtime checks
trace on the Build Gun channel before and after moving a connected machine, after
repeated transforms, and after replaying received geometry. They also check physics
registration and duplicate collision boxes. Windows Shipping, Linux Server Shipping
and FactoryEditor compilation succeeded, and all five editor tests passed. Both
0.3.3 archives were packaged and their native binaries matched the fresh builds by
SHA-256. The new packaged targeting checks still need execution; replaying geometry
locally does not verify multiplayer replication. Version 0.3.3 was installed on September 30 and all seven installed files matched
the release archive by SHA-256. Packaged runtime testing was deferred while the user was playing.
Development Windows packaging explicitly selects Steam to exclude stale Epic
binaries from the shared build cache.

The runtime test blocks world saves and never loads an existing world save. Its
hooks activate only with `-BuildingSkillsRuntimeTest`; normal play is unaffected.
Logs and screenshots are `Saved/RuntimeTest.log`, `Saved/RuntimeTest.png`,
`Saved/TransformTest.png`, `Saved/NetworkTest.png`, and `Saved/BlueprintTest.png`,
relative to this repository. Reproduce with `scripts/RuntimeTest.ps1 -Render` while
Satisfactory is closed. Use `scripts/VerifyInstall.ps1` to recheck installed files.
For the focused network fixture, use `scripts/RuntimeTest.ps1 -Render -NetworksOnly`.
For the focused blueprint fixture, use `scripts/RuntimeTest.ps1 -Render -BlueprintsOnly`.

The original startup-only checks missed two gameplay failures: the input guard
rejected the Build Gun's own interaction widget, and the requested Edit mapping
context was not activated. The guard now respects the widget's native blocking flags;
runtime context copies are registered directly with the enhanced-input subsystem.
The fixed keyboard shortcut list is extended with an SML widget hook rather than
relying on enhanced-input hint metadata.

Editor automation additionally covers nested groups, group merges retaining children,
one-level ungrouping and invalid selections, hierarchy normalization, save-property
serialization, modifier remapping, rebindable commands, and the scoped M/Map conflict.
Editor FactoryGame methods are stubs, so these tests cannot replace packaged-game checks.

The reported group-hover crash exhausted Unreal's UObject limit in the native
refund UI update called from `AddPendingDismantleActor`. Selection batches now
suppress those intermediate refund updates, Edit skips refunds entirely, and
unchanged groups retain their pending list. Spatial lookup no longer rebuilds a
whole building class each frame. The regression records UI-widget growth separately
from total UObject slots, because unrelated world streaming can allocate objects
while the player remains stationary.

## Remaining manual coverage

Transformed-building save/reload, collision and snapping across all supported building
types, multiplayer/late join, every scale-axis visual, and transformed blueprint
membership need manual coverage. Multiple placed blueprint expansion, complex nested
selections, live rebinding, and uninstall compatibility also need the matrix below.
The runtime fixtures use storage containers, lightweight foundations, belts, lifts,
pipes, rails, power towers, wires and a miner with default keys; they do not claim
those broader cases or benchmark an existing
large factory. This build has not been packaged for Epic or Windows Server.

Run `scripts/Mod.ps1 BuildEditor`, `scripts/Mod.ps1 EditAssets`, and
`scripts/Mod.ps1 Test` for editor checks. Reports are in `Saved/Automation`.

## In-game test matrix

Use a separate test save containing machines, foundations and two placed blueprints.

1. Confirm the bottom-right reminder includes the configured Edit shortcut (Alt+E
   by default), including after rebinding it. Open Edit from both holstered and
   equipped states; the left panel must say EDIT and the bottom hints must offer
   Group/Merge. Hover a large existing group in Edit and vanilla dismantle, then move
   between its machines and foundations; neither should freeze or crash.
   Hold the configured multi-dismantle key (default Ctrl) and
   aim across two machines and two foundations. Release it and press Group (G).
   Confirm four buildings are highlighted, one group is selected, and left click
   cannot dismantle anything in Edit.
2. Switch with the configured mode key (default R) through Normal, Blueprint and
   Group. Previously selected items must remain selected. The HUD must follow both
   the mode and the configured key names.
   After pinning the first item, aim at an unselected object, blueprint and group.
   Each hovered target must highlight without joining the pinned selection. Repeat
   while another player changes the hovered group's contents.
3. Make a second group. In Group mode select the first group, switch to Normal,
   select an ungrouped building, and press G. Confirm a new outer group contains
   the first group and the single building.
4. Select exactly that outer group and press U. Confirm the child group still exists
   and the single building is ungrouped. Repeat with several levels: each U removes
   only the selected outer level.
5. Select a group plus a single and press M. Confirm the single joins the existing
   group without a new parent. Select two groups and press M: their outer containers
   combine, while their child groups remain. Select two groups and press G instead:
   both original groups must remain under the new parent.
6. Select multiple placed blueprints using Ctrl. Switch to Normal and add singles;
   switch to Group and add groups. Confirm selection persists throughout. G treats
   blueprint members as singles, preserving vanilla blueprint membership.
7. U must be absent/inactive with singles, multiple groups, or a group plus an
   unrelated single. Repeated selections and overlapping blueprint/group selections
   must not duplicate buildings or refunds.
8. Close Edit, enter vanilla dismantle (F), and repeat the multi-blueprint and
   mixed-mode selection tests. Group must be offered alongside Normal and Blueprint.
   First confirm that simply aiming at one machine/foundation highlights it and
   holding dismantle completes. Then repeat with a persistent multi-selection while
   moving the cursor: its hold timer must still complete.
   Dismantle a nested group and verify all selected descendants disappear, materials
   are refunded once, and unrelated buildings remain. Check cancelled dismantles,
   disqualified buildings, full inventory, and lightweight foundations/walls.
9. Rebind the existing multi-dismantle and Build Gun mode actions, and all
   Building Skills actions. Repeat the tests. M must open Map outside Edit, trigger
   Merge only inside Edit, and work normally after closing Edit. Plain E must still
   interact. Check both Alt+E release orders.
10. Save and reload nested mixed actor/lightweight groups. Delete a member, create a
    replacement elsewhere, save and reload again. Surviving members must remain in
    the correct groups; deleted members and reused lightweight slots must not adopt
    unrelated replacements. Exact coincident copies should fail safely as ambiguous.
11. Check switching to build/paint, holstering, interaction UI, pause, vehicle entry,
    death/respawn, portals, reconnect, and a new session. No stale selection, pinned
    temporary actors, Edit-only bindings, or duplicate HUD should remain.
12. Repeat as a listen host and remote client. Group changes must agree, including
    concurrent edits and late joins. Save on the host and reload. Dedicated server
    gameplay also needs verification on the packaged Linux dedicated server.
13. On a copy of the save, remove the mod, load, save again and reload. Compare base
    building counts, transforms, inventories, recipes and connections. Group metadata
    is expected to be unavailable; do not expect reinstalling to recover it from a
    save written without the mod.


## Transform manual checks

1. Select separate buildings, a blueprint, and nested groups in turn. Press T;
   the selection must stay fixed while looking around and holding the selection key.
2. Cycle all ten modes with the configured mode key. The colored rings/arrows and
   bottom hint must follow the active axes. Scroll both directions and verify the
   intended shared pivot and size changes. Check collision and subsequent snapping.
3. Turn to face each cardinal direction and try all arrow keys, then Page Up/Down.
   Rebind T, movement, modifiers and the normal Build Gun mode key; check the hints.
   Change each tier's movement, rotation, multiplier and repeat delay in mod settings.
4. Press Esc once: stay in Edit, retain the selection and changes, and restore Group,
   Merge, Ungroup and selection-mode controls. Re-enter Transform and repeat.
5. Move, rotate and scale connected machines, belts/lifts, pipes, rails, wires,
   stations and extractors. Internal links must remain connected; boundary splines
   must stretch to fixed external endpoints. Check items, fluids, train movement,
   pipe indicators and resource bindings before and after the edit.
   After moving a connected machine, target its stretched belt in both Edit and
   dismantle on the remote client. Repeat after another move and after reconnecting.
6. Save and reload a separate test world after mixed transformations; verify native
   transforms, inventories, group membership and blueprint selection. Then repeat
    with a host and client, late joining, and concurrent edits.

7. Cycle P through Bounds, Anchor, Individual and Origins; change the anchor with B.
   Toggle L between world/local axes, including on quarter-turned and oblique buildings.
   Shared center pivots must not drift during inverse operations. Individual local
   scaling must keep each origin fixed. Rebind all three controls and check the hints.
8. Undo and redo with Z/Y after moving a machine with attached belts, pipes or wires.
   Check endpoint geometry, targeting and contents. Undo edits to A, then B, then A;
   every step must remain accessible. A new edit clears redo. Dismantling, reconnecting
   or another player's conflicting transform must prevent unsafe history restoration.
9. Transform independent selections from two clients, then deliberately overlap them.
   Independent edits must succeed; stale overlapping edits must be rejected. Under
   latency, queued movement should preview promptly and settle on the server result.
   A superseded reply must release the local selection instead of waiting indefinitely.
   Join while many previously edited buildings are being transformed; older snapshot
   chunks must not overwrite newer geometry or disconnect the client.

## Blueprint saving manual checks

1. Select buildings or nested groups containing machines and lightweight buildings. Alt+S / Save
   must appear in the bottom hints and open the native blueprint designer window.
   Check name, description, icon, color and category; Load and Clear must be absent.
2. Save a uniquely named blueprint and place it elsewhere. All selected descendants
   must appear once, with their relative transforms and connections preserved.
   The window must report the disk-write result. The original buildings, group and
   pre-existing blueprint memberships must remain unchanged.
3. Save using an existing name and check the native overwrite confirmation. Cancel
   it once, then confirm it. Close the window without saving and verify Edit resumes
   with the same selection.
4. Check Save with singles and multiple groups. Enter Transform or clear the
   selection: Save must be unavailable. Plain S must retain its usual movement
   behavior throughout. Rebind Save and repeat.
5. Repeat on a Linux dedicated server with two Windows clients. Verify blueprint
   availability on the saving client, independent windows for both players, and
   rejection if another player changes the selected group while its window is open.
   An unrelated edit elsewhere must not prevent saving this group.

## 0.8.0 polish validation

The installed Windows candidate passed 43 checks in the isolated rendered
`RuntimeTest.ps1 -PolishOnly -Render` run. The test loaded a new temporary world
with saving blocked, exited normally, and did not load a user save. Evidence:
`Saved/Runtime080-Polish3.log` and `Saved/PolishPose.png`.
The left transform panel was also inspected visually.

Covered in that packaged run:

- Deleting a group's final lightweight members removes the group immediately.
- Transform starts in Local and arrow movement follows a diagonal reference frame.
- Alt+A enables Anchor axes and anchor-relative pose readouts. B is labeled Reference.
- A single object and a complete blueprint show position, rotation and scale in the left panel.
- Ctrl+Z appears in native Build/Dismantle hints; leaving the gun releases its input context.
- One native batch deletion consumes one history entry, including a batch of foundations.
- Undo restores inventories and groups and repays the native dismantle refunds exactly.
- Spent refunds and a moved outside connection reject restoration without consuming history.
- Native construction creates a separate per-player history entry and can be undone.
- Undo reconnects a pipe and a constructor's cable to unselected endpoints.
- A native blueprint regains its proxy, member identity and original pose.
- A block signal regains guarded/observed ownership on existing rails.
- A rail and its signal restore together and reconnect to the unselected track.

Dedicated-server multiplayer, restored conveyors carrying items, overflow-refund
containers, and a subsequent world save/reload are not established by this local
run. The final editor automation run passed all 20 tests (0 warnings, 0 failures),
including action-count retention, frame-relative movement and nested-group pruning.
Evidence: `Saved/Automation/index.json`.

Windows and Linux Server 0.8.0 archives passed ZIP integrity, descriptor-version,
architecture and fresh-native-binary SHA-256 checks. The installed Windows copy
matched all 8 packaged files by SHA-256. Linux Server compiled and packaged
successfully; its package has not been exercised on a dedicated server. Evidence:
`Saved/Releases/BuildingSkills-0.8.0-verification.json` and
`Saved/InstallationVerification.json`.

The final editor and Linux native builds also exercised the two-worker compile
helper successfully. Each helper compiled the current linker inputs before linking.

Use `RuntimeTest.ps1 -PolishOnly` for the dedicated temporary-world regression.

# 0.10.7 Stick and planner corrections

Stick now loads the active runtime input contexts instead of the empty legacy
contexts left by the shared-binding migration. This restores its configurable
toggle, scrolling modifiers and Settings mapping in spline placement and Plan.
The packaged Stick regression also checks that the activated context actually
contains the toggle action. Tests are not being run for this update at the
user's request; gameplay validation is left to the user.

The same update stabilizes path orientation when extending the back, preserves
existing node directions during extension, and resolves lightweight foundations
before applying the native floor snap. These planner changes have not been
gameplay-tested in this update either.

Editor, Windows and Linux native compilation succeeded. Windows 0.10.7 was
installed and all eight installed files matched its package by SHA-256; both
release archives matched the compiled native outputs. Evidence is in
`Saved/Releases/BuildingSkills-0.10.7-verification.json` and
`Saved/Releases/BuildingSkills-0.10.7-installation.json`. No game was launched.

## 0.10.8 defaults fixes

Built and installed with 0.10.8. Gameplay validation is left to the user.

- Stick settings load on first use after configuration registration instead of
  during early world-subsystem initialization. All Stick and Plan consumers use
  that accessor. Temporary settings remain in the world session afterward.
- Stick and Volume reject invalid numeric fields individually, preserving the
  other saved fields. The defaults editor uses the same bounds as runtime for
  movement, rotation, scaling, repeat delay, Connected gap, Volume and Stick.
- Connected defaults retain invalid regex text for the existing validation
  message instead of silently clearing every setting and filter.
- New plans carry the creating player's build defaults to the server, which
  validates them. Action-history size remains server-controlled.

Source audit: Transform reads defaults on use unless temporary steps exist;
Volume reads them on entering Volume; Connected reads them when player input is
initialized; history reads the authority's setting on use; new-plan defaults
apply only when creating a plan. Existing plans retain their saved specification.
Volume color uses the native game setting independently of the mod configuration.

Also included: the single-spline K-menu button **Reverse spline direction**.
It reverses endpoints, control order and tangents without editing shared nodes,
and runs through the existing server command/revision checks and undo/redo
history. It compiled with 0.10.8; gameplay tests have not been run.

## 0.10.8 parallel and saved-plan changes

These changes compiled for Editor, Windows and Linux server. Windows is
installed; gameplay testing is left to the user.

- Parallel node markers follow their source nodes, including selection and
  dragging. Signed local-X node offsets deform the parallel while retaining
  endpoint tangent continuity.
- Signed rail start offsets trim the beginning or extend backward along its
  tangent. They apply once per connected path; native length and shape limits
  still apply to construction.
- Saved plans load on selection. Double-clicking the name enters inline rename.
  Pending auto-saved edits finish before switching plans.
- Duplicate preserves geometry and specifications, generates independent graph
  identities and selects a personal `<plan name> (copy)`. Server visibility,
  revision and plan-count checks apply.

Manual checks: verify curved and branched parallels, closed loops,
positive/negative offsets and live rail joints. Verify click-to-load,
double-click rename, duplicate independence, shared-plan permissions and plan
switching while a rename or geometry update awaits a server response.

### 0.10.8 build and installation evidence

Editor, Windows client and Linux server compilation and packaging succeeded.
Editor header generation initially reported an unused access transformer; a
retry succeeded without changing the declaration or shared SDK.
All eight installed Windows files matched the package by SHA-256. Both release
archives contain version 0.10.8, the required cooked assets, and native binaries
matching the successful builds. Receipts:
`Saved/Releases/BuildingSkills-0.10.8-installation.json` and
`Saved/Releases/BuildingSkills-0.10.8-verification.json`.
No automated tests or gameplay sessions were run for this release.

## 0.10.9: defaults reflection

The 0.10.8 initialization fix did not address SML's reflection write filter.
`FillConfigurationStruct` writes only Blueprint-visible properties that are
not Blueprint-read-only. Plain `UPROPERTY()` fields silently retain their
compiled defaults. This affected all three configuration destination structs:
Stick, Volume and Transform (also carrying Connected, History and Plan defaults).
The structs now use `BlueprintType`, and their configuration fields use
`BlueprintReadWrite`.

`BuildingSkills.Configuration.SavedDefaultsReflection` exercises the actual
SML section-to-reflected-struct bridge using a private configuration instance.
It checks every numeric, boolean and string field, including a Stick step of
10 and disabled horizontal constraint, then refreshes the same reflected
wrapper with different values. It also checks every schema option has a tested
consumer. The fixture does not modify the player's settings or files.

The focused editor regression passed (one test, no failures or warnings).
Report: `Saved/Automation-defaults-0.10.9/index.json`.
Editor, Windows client and Linux server compilation and packaging succeeded.
Windows 0.10.9 is installed; all eight installed files matched its package by
SHA-256. Both release archives contain the required cooked assets and native
binaries matching the compiled outputs. Receipts:
`Saved/Releases/BuildingSkills-0.10.9-installation.json` and
`Saved/Releases/BuildingSkills-0.10.9-verification.json`.
No gameplay session or player save was opened. Packaged gameplay validation
is left to the user.

## 0.10.9 public-release preparation

The Steam client and Linux server native binaries from the verified 0.10.9
build are reused unchanged. Epic client and Windows server native compilation
completed against the same installed SDK. The Windows server target needed a
previously uncached resource file before linking; the native build helper now
compiles Windows resources explicitly. All four targets were cooked and combined
by Alpakit.

The first merged archive was rejected by the publication validator: stale shared
target receipts excluded the Steam and Linux native binaries even though
Alpakit reported success. Refreshing those target records and repackaging fixed
the omissions. Packaging now checks all requested target receipts before staging.
No other mod source or shared toolchain source was changed.

`scripts/PreparePublication.py` verified the corrected archive's three platform
folders, four native binaries against the local compiled outputs, SML module
manifests, descriptors, required Edit/Plan cooked assets, nonempty cooked payloads,
MIT/ICU/SML notices and icon. CRC checks passed. Debug symbols are omitted from
the public upload, with the original full Alpakit archive retained locally.
Three malformed-path/layout cases were rejected by the validator.

The output is `Saved/Publication/0.10.9/BuildingSkills.zip`; the accompanying
`verification.json` and `SHA256SUMS.txt` record exact hashes. The source snapshot
includes the publication changes. This preparation did not install the mod,
launch gameplay, or load a player save. Epic and Windows server runtime behavior
has not been tested in this preparation.

Six gameplay clips supplied by the publisher were converted to looping GIFs
for the short publication overview. Every GIF frame was decoded successfully,
and representative frames from each clip were visually inspected. The media
manifest records source/output hashes, trims, playback speeds and dimensions.
The original recordings were preserved. This media validation does not add
gameplay regression coverage.
