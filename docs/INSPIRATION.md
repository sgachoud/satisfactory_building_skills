# Feature comparisons and implementation provenance

Reviewed 2026-10-01 for Building Skills 0.4.4. This records both the user-requested
inspirations and the implementation comparisons made during the review. A similar
feature is not evidence that another mod's code was used. The changes in this
review were written independently; no third-party source or assets were imported
into the distributed plugin, and the project's MIT license was not changed.

## Sources and license boundaries

The 0.9.8 Stick endpoint correction follows the installed game's native
pipe/belt placement state machines. Their final click consults the root's end
snap, while Building Skills transfers that snap to the final split segment.
The independently written adapter scopes that reference around the native click
only; native routing, validation and construction remain responsible for the
placement. No other mod's source or assets were copied for this correction.

The 0.6.5 preview-cost change follows the installed native recipe-manager lookup
and Unreal's World/GameState lifecycle. Read-only inspection established that
native recipe lookup uses GameState, while `UWorld::SetGameState` also broadcasts
events and updates level collections. The replacement scopes only the reflected
preview-world pointer around pricing, with automatic restoration; it removes
the global recipe-lookup interception. No other mod's code was used.

The 0.6.4 Linux cost-hook investigation reviewed SML 3.12's
`NativeHookManager.h` at revision `d2162c999bbaa9650594ee9ad6e6e06190306009`
([source](https://github.com/satisfactorymodding/SatisfactoryModLoader/blob/d2162c999bbaa9650594ee9ad6e6e06190306009/Mods/SML/Source/SML/Public/Patching/NativeHookManager.h),
repository license GPL v3). It identified a Windows-specific aggregate-return
assumption. Building Skills uses the existing SML registration and handler-list
APIs with a mod-local typed Linux dispatcher; the shared SDK is unchanged.
Clang's Linux-target output and the rebuilt binary were checked against the
[SysV AMD64 calling convention](https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf).

The 0.6.2 hint repair uses the installed game's `Widget_HintContainer` and
`Widget_Hint` interfaces, including the native entry appearance animation.
Read-only SDK asset exports established their layout and lifecycle. The mod
references the game's existing widgets; it does not distribute copies of them.
Filled volume boundaries use Unreal's existing cube/sphere primitives with the
mod's own translucent material, following the user's requested appearance.

The 0.4.7 constructor-cover investigation used the installed SDK's constructor
decoration template and `FGBackgroundThread` API, plus read-only inspection of
the corresponding game lifecycle. This identified pooled fog planes outside the
actor's component hierarchy. The candidate refresh uses native queued removal
and registration; no other mod's implementation or assets were used for this fix.

| Reference | Source revision and license checked | How it was used |
| --- | --- | --- |
| Infinite Nudge | [SirDigby's collection, `158210c`](https://github.com/mklierman/SatisfactoryMods/tree/158210c61bd7e7953a40eb0f1692a8349d99a008/InfiniteNudge), [GPL v3](https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/LICENSE.txt) | User-requested inspiration for camera-relative movement, adjustable steps and active-axis feedback. Native input, configuration, gizmo and scale RPC implementations reviewed. |
| Micro Manage | [`1afef6c`](https://github.com/TwoTwoEleven/MicroManage/tree/1afef6c25d75e281442e8b4660224ee9258f86d9), [GPL v3](https://github.com/TwoTwoEleven/MicroManage/blob/1afef6c25d75e281442e8b4660224ee9258f86d9/LICENSE) | User-requested transform comparison. Selection, anchor/pivot math, undo storage, disconnected scene components, wire updates and RPC implementations reviewed. This is historical code, not evidence of current game compatibility. |
| Rearchitector Redux | [`158210c`](https://github.com/mklierman/SatisfactoryMods/tree/158210c61bd7e7953a40eb0f1692a8349d99a008/RearchitectorRedux), [its own GPL v3 license](https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/RearchitectorRedux/LICENSE) | User-requested comparison for independent versus shared-pivot transforms, bounds, selection outlines and undo. Native target manager, actions, outline utilities and RCO reviewed. Several lightweight paths in this revision are explicitly disabled following game API changes. |
| Quick Blueprint (`BlueprintHighlighted`) | [source at `158210c`](https://github.com/mklierman/SatisfactoryMods/tree/158210c61bd7e7953a40eb0f1692a8349d99a008/BlueprintHighlighted), collection GPL v3 | User suggested this for saving existing selections. Its native connector/settings library and exported Build Gun mixin, RCO and popup Blueprint graphs were reviewed. The graphs show a name popup, a server handoff, archive writing, temporary copies, and configurable first/average/zero rotation. |
| Infinite Dismantle | [source at `158210c`](https://github.com/mklierman/SatisfactoryMods/tree/158210c61bd7e7953a40eb0f1692a8349d99a008/InfiniteDismantle), collection GPL v3 | Reviewed its native dismantle-limit hook as a comparison for large selections. |
| Deselect Mass Dismantle | [source at `158210c`](https://github.com/mklierman/SatisfactoryMods/tree/158210c61bd7e7953a40eb0f1692a8349d99a008/DeselectMassDismantle), collection GPL v3 | Exported its keybind actor graph. It checks whether the aimed actor is in the native pending array and removes it. This confirms the basic deselection pattern; our gesture operates on persistent groups/blueprints and respects configured input. |
| Set Blueprint Origin | [source at `158210c`](https://github.com/mklierman/SatisfactoryMods/tree/158210c61bd7e7953a40eb0f1692a8349d99a008/SetBlueprintOrigin), collection GPL v3 | Reviewed the archive-origin hook as a comparison for blueprint centering. |
| Persistent Paintables | [source at `158210c`](https://github.com/mklierman/SatisfactoryMods/tree/158210c61bd7e7953a40eb0f1692a8349d99a008/PersistentPaintables), collection GPL v3 | Reviewed native subsystem authority, construction hooks and pipe-network customization updates as adjacent comparisons for preserving native state. It is not an implementation of nested groups. |
| Smart! | [repository and source-use notice](https://github.com/majormer/SmartFoundations) | User-requested visual inspiration for a left-side status panel and bottom controls. Current notice restricts reuse in other mods; its implementation was excluded from this review. No Smart code or assets were incorporated in these changes. |
| Area Actions | [`c44404b`](https://github.com/mircearoata/SatisfactoryAreaActions/tree/c44404b518c500dad663a0685e4e9d07c995c887), [GPL v3](https://github.com/mircearoata/SatisfactoryAreaActions/blob/c44404b518c500dad663a0685e4e9d07c995c887/LICENSE) | Investigated as a group/area-action candidate. This revision exposes Blueprint assets rather than native implementation files; it was not used as implementation evidence or as a source for changes. |
| Build Gun Utilities | [author's public repositories](https://github.com/SifVerT), [manual](https://github.com/SifVerT/SifDocs/blob/main/Pages/BuildGunUtilities.adoc) | Found as an adjacent placed-building manipulation tool. No reusable implementation source was established; not used for code changes. |

GPL permits source study and reuse subject to its conditions. It does not mean
GPL implementation can simply be copied into an MIT-only distribution. These
comparisons use behavior, architectural observations and native API checks;
copied/adapted GPL code would require a separate licensing decision. Third-party
assets and branding are not reused. Reference checkouts remain in ignored research
storage and are not build or runtime dependencies.

## Feature-by-feature findings

The linked source directories below are pinned to the reviewed revision.

| Building Skills feature | Comparable implementation / inspiration | Decision or improvement |
| --- | --- | --- |
| Alt+E Edit mode, left panel, native bottom hints and shortcut helper | Smart visual layout requested by the user; [Infinite Nudge input lifecycle][nudge-input] binds Enhanced Input actions when player input initializes. | Retain per-player Edit contexts and native hint integration. Do not use Smart source or duplicate another mod's assets. |
| Configurable G/M/U/S/T and transform keys; configured vanilla Ctrl/R | [Infinite Nudge module][nudge-module] resolves current action mappings and chord modifiers; input module handles press/release actions. | Existing mapping lookup and modifier precedence already follow the desired behavior. Retain normal movement S when Save is unavailable. |
| Persistent Normal/Blueprint/Group selection, multi-select and removal gestures | [Micro Manage selection][micro-selection] separates target, anchor and selected set; [Redux target manager][redux-manager] maintains an explicit target collection. | Keep hover distinct from the persistent selection. Native blueprint members remain individual objects for grouping. No equivalent nested-group implementation was found in the reviewed sources. |
| Independent hover highlight, group highlight and lightweight targeting | [Redux outline utility][redux-outline] uses outline proxy instances for shared meshes; Micro Manage has separate target/anchor materials. | Retain separate native outline owners and current lightweight identity resolution. Avoid replacing real materials or reviving disabled legacy instance-handle paths. |
| Large selection and native dismantle/refunds | [Infinite Dismantle][dismantle] raises the native selection limit every state tick. Micro Manage provides larger selected sets. | Keep the explicit 2048-member operation limit and native dismantle/refund path. Increasing the UI limit alone would not make large RPCs safe. |
| Nested G groups, M merge and one-level U ungroup | Closest comparisons are Micro Manage/Redux flat target sets. The hierarchy semantics came from the user's design and subsequent clarifications. | Added indexed descendant traversal and parent validation. Deep nesting no longer repeatedly scans the full tree for every ancestor. Member output order, deduplication, cycle detection and merge/anchor semantics are preserved. |
| Group save/reload, member cleanup and identity after lightweight compaction | Persistent Paintables provides an adjacent subsystem/authority comparison; Redux's target representation retains live actor/hit references. | Keep saved group GUIDs and native actor references, plus lightweight class/recipe/transform fingerprints. A transient hit index cannot substitute for persistent identity. No matching saved nested-group implementation was found. |
| T transform state and Esc return | Infinite Nudge operates on the active hologram; Redux uses its own manipulation equipment/actions. | Keep a separate Edit transform context for already-placed objects and preserve selection on return. No replacement needed. |
| R axis modes, scroll rotation and scaling, active-axis display | [Infinite Nudge modes/gizmo][nudge-gizmo] supplies the requested interaction model. | Retain per-player modes, uniform XYZ scaling, paired-axis scales and independently drawn axis feedback. Do not adopt its global static active-mode storage. |
| Facing-relative arrows, vertical Page keys, modifier steps and repeat | Infinite Nudge configuration/mapping logic; [Micro Manage transform math][micro-transform] computes view-relative axes. | Existing nearest-horizontal-axis movement matches the user's choice. Keep reciprocal scale steps and configurable modifier precedence. |
| Shared bounds/origins/anchor pivots; individual pivots; world/local frames | [Redux target manager][redux-manager] distinguishes bounds and origin pivots; Micro Manage supplies anchor-based group and independent operations. | Retain existing options and fixed shared pivots. Keep the current shear-free nonuniform scale projection, including reversible oblique transforms. |
| Queued input and local transform preview | Micro Manage and Redux issue transform actions; their inspected code does not supply our bounded pending-command protocol. Comparing action boundaries prompted a queue edge-case review. | Fixed cancelling queued arrow input in scaling modes: remove the cancelled command before zero movement can be interpreted as zero scale. Reciprocal queued wheel steps also disappear, avoiding redundant server edits/history entries. |
| Undo/redo and continuous gestures | [Micro Manage undo][micro-undo] stores bounded transform snapshots; Redux actions support undo. | Retain server-owned before/after geometry, gesture coalescing, redo, memory limits and conflict checks. Client-provided snapshots are not authoritative. |
| Native transform persistence, inventories, recipes, customization and detached components | Micro Manage manipulates actor/rootless component transforms; Redux refreshes instanced rendering; Persistent Paintables calls native customization setters. | Keep in-place native changes and separate detached-component updates. Native world saving retains these properties without another saved copy of each building. |
| Belts/lifts, pipes, rails and wires stretched to unselected endpoints | [Micro Manage RCO][micro-rco] gathers/recreates wires; [Quick Blueprint connector library][quick-connectors] handles factory, pipe and rail connectors when duplicating selected objects. | Preserve wire identity and existing connections, rebuilding geometry in place. Keep distance-weighted boundary spline changes, collision refresh, conveyor-chain rebuilding and fluid cache refresh. Recreating objects would endanger identity and group references. |
| Extractor resource bindings, train-platform owned tracks and spline attachments | Reviewed transform tools are only partial analogues; Redux's target filter excludes some conveyor-related types. | Retain our specialized native handling. No reviewed alternative demonstrated broader support that could safely replace it. |
| Server validation, batching, delayed objects and late join | Micro Manage uses an RCO and multicast; [Redux RCO][redux-rco] distributes transform actions; Infinite Nudge sends hologram scale to authority. | Retain bounded reliable snapshots, per-object sequence checks, expected-state validation and late-join handling. Simple multicast is insufficient for clients joining after an edit. Real dedicated-server behavior still requires multiplayer testing. |
| S opens native save UI, no Load/Clear, overwrite and metadata | Quick Blueprint was the user's functional inspiration; its exported popup and RCO graphs connect name entry to a server save event and client completion. | Keep our owner-only native designer session, request IDs, expiry and native save RPC forwarding. UI/save correctness is supported by our packaged regressions, not inferred from another mod. |
| Selected blueprint content, connection/settings preservation and disk validation | [Quick Blueprint library][quick-connectors] explicitly restores connector directions, splitter rules and other settings for copies. | Keep native serialization of original objects with scoped designer membership, rather than maintaining a list of properties to copy. Retain completed archive count/transform validation and actual disk-result reporting. |
| Upright majority alignment, tight bounds and bottom-center origin | [Set Blueprint Origin][blueprint-origin] changes archive origin using buildable elevations; Redux computes component bounds for pivots. The majority-first/yaw-only policy came directly from the user, who subsequently requested lowest-Z placement to follow native designer conventions. | Keep transformed geometry bounds, deterministic majority yaw and tight fit; center X/Y and use the lowest geometry point for Z. Quick Blueprint's exported GetTransform graph uses average X/Y, lowest Z, and a configurable first/average/zero rotation. Its origin is a useful comparison, but our horizontal center and alignment are geometry-based. |
| Scale preserved in ordinary and group blueprint save/load | Infinite Nudge scales holograms and children, and its RCO updates authoritative scale. This is an adjacent comparison, not native blueprint archive preservation. | Keep the native actor-header scale field, completed-buffer patch and scoped load correction. No reviewed mod supplied a matching end-to-end blueprint scale implementation. Keep file compatibility without claiming vanilla restores scale. |

## Scope and evidence limits

Searches included current source pages and the pinned repositories above. Finding
no equivalent means none was established in this review, not that no such mod
exists. Quick Blueprint and Deselect Mass Dismantle graphs were exported to text
and their relevant nodes and links inspected. The export commandlet reported
missing third-party native classes/access transformers: the reference projects
were not built or validated as runnable mods, and their asset files remained
unchanged. Area Actions graphs were not inspected. Reference checkouts were not
deployed into the game or added to the shared SDK's plugins. Runtime regressions
use the game's existing installed mod set.

The review produced two focused changes, not a rewrite of working features.
New editor regressions cover a 2048-level hierarchy, ordering, cycles, missing
parents, duplicate membership and cancelling queued input across all ten modes.
The packaged harness also exercises cancellation through the actual Edit component.
See [TESTING.md](TESTING.md) for the build and runtime results, including platform
and multiplayer limits.

[nudge-input]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/InfiniteNudge/Source/InfiniteNudge/Private/InfiniteNudgeGameInstanceModule.cpp
[nudge-module]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/InfiniteNudge/Source/InfiniteNudge/Private/InfiniteNudgeModule.cpp
[nudge-gizmo]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/InfiniteNudge/Source/InfiniteNudge/Private/InfiniteGizmo.cpp
[micro-selection]: https://github.com/TwoTwoEleven/MicroManage/blob/1afef6c25d75e281442e8b4660224ee9258f86d9/Source/MicroManage/MicroManageSelection.cpp
[micro-transform]: https://github.com/TwoTwoEleven/MicroManage/blob/1afef6c25d75e281442e8b4660224ee9258f86d9/Source/MicroManage/MicroManageTransform.cpp
[micro-undo]: https://github.com/TwoTwoEleven/MicroManage/blob/1afef6c25d75e281442e8b4660224ee9258f86d9/Source/MicroManage/MicroManageUndo.cpp
[micro-rco]: https://github.com/TwoTwoEleven/MicroManage/blob/1afef6c25d75e281442e8b4660224ee9258f86d9/Source/MicroManage/MicroManageRCO.cpp
[redux-manager]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/RearchitectorRedux/Source/Rearchitect/Private/ArchitectorTargetManager.cpp
[redux-outline]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/RearchitectorRedux/Source/Rearchitect/Public/ActorUtilities.h
[redux-rco]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/RearchitectorRedux/Source/Rearchitect/Private/ArchitectorRCO.cpp
[dismantle]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/InfiniteDismantle/Source/InfiniteDismantle/Private/InfiniteDismantle.cpp
[quick-connectors]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/BlueprintHighlighted/Source/BlueprintHighlighted/Private/BHConnectorBuildableLibrary.cpp
[blueprint-origin]: https://github.com/mklierman/SatisfactoryMods/blob/158210c61bd7e7953a40eb0f1692a8349d99a008/SetBlueprintOrigin/Source/SetBlueprintOrigin/Private/SetBlueprintOrigin.cpp

## 0.4.7 selection Save and Copy

The user requested Save for arbitrary selections and Copy modeled on vanilla
blueprint sampling. Implementation uses the installed native SDK's
`WriteBlueprintToArchive`, `LoadStoredBlueprint`, `AFGBlueprintHologram`, and Build
Gun blueprint state. These APIs provide serialization, hologram visuals, costs and
construction; transient player-owned clipboard transport and fresh nested-group
identities are original Building Skills code. No third-party mod code was copied
for this feature. Native header/cache and callback behavior was checked against the
installed game's exported functions; these observations are not gameplay validation.

During installation testing, the pinned Quick Blueprint save graph and native
connector library, Micro Manage's root-component transform loop, and Redux's
instance rendering paths were rechecked. Their GPL v3 licenses were verified
from the reference repositories. None of these inspected paths supplies a
transient clipboard-to-native-gun handoff or a refresh of the current game's
pooled constructor fog planes. Quick Blueprint confirms the native archive API
choice, but its saved-blueprint flow does not provide the temporary Copy lifecycle.
The Copy state handoff and pooled-decoration fixes therefore use native SDK
lifecycle behavior; no GPL implementation was copied or adapted into this MIT
plugin. The references and exact revisions are listed above.

## 0.4.8 blueprint anchor

The Alt shortcuts, excluded/included anchor colors, complete orientation frame,
and optional lower floor come from the user's specification. The earlier Micro
Manage/Redux anchor and local-frame comparisons remain relevant background, but
their inspected transform tools were not a source for this blueprint feature.
Building Skills fits selected geometry in the native anchor rotation and uses
native outline proxies with a separate instance of the game's colored-outline
post-process material. No third-party mod code or assets were imported.

The temporary step editor follows the user's requested M shortcut and session
lifetime. It uses the native interact-widget stack and Unreal's numeric entry
widgets. Its fields share Building Skills' existing step tiers and validation;
the earlier Infinite Nudge comparison informed those tiers, not this menu's code.

## 0.5.0 Connected Components

The bounding-box traversal, gap, filters and reuse in selection/painting follow
the user's specification. The K menu convention was requested for consistency
with Smart; no Smart implementation or assets were copied. The search and bounds
tree are original implementation. Regex uses the engine's existing ICU dependency,
including its native syntax diagnostics and time/stack limits. Its permissive
copyright/permission notice is shipped in `Resources/ICU_64-1_License.txt`. Painting delegates
to the installed game's native single-target customization routine after resolving
the connected set. The settings UI reuses Building Skills' own tested Slate/native
interact-widget pattern. Transform settings now defaults to K as requested.

## 0.5.1 HUD and shortcut interoperability

The missing-control fix comes from tracing Building Skills' own hint-widget
lifetime across gun states. Supplemental Dismantle and Customizer hints retain
the native controls in a separate bottom row. The Smart source-use notice permits
viewing and study; a limited interoperability inspection identified its public
settings action and the active-hologram condition for K. No implementation or
assets were copied. Runtime tests exercise the installed mod's real menu through
keyboard input, alongside Building Skills' scoped contexts.

Connected paint previews use the game's native customization-preview registry,
material-preview helper and outline component. Temporary previews leave stored
customization unchanged and restore through native cleanup. The Spread label
uses the native mode row, scoped to the current Customizer widget. These changes
were written from the installed SDK and runtime observations; no implementation
from another mod was incorporated.

## 0.5.2 native mode radials and modal keyboard input

The R hold behavior follows the game's existing Build Gun timer, supported-mode
descriptors and native radial widget. Custom Transform and spread choices are
handled locally without replacing the replicated selection mode. Runtime tracing
identified the native first-item assignment after taps and the deferred selection
commit after radial release; local tools distinguish those paths. The input fix
uses Slate's child-first key routing: fields handle text editing, then the modal
consumes unhandled keys before they reach world shortcuts. Regex keeps ICU
syntax; readable building names replace the internal-class fallback. These fixes
come from the installed SDK and observed native behavior, not another mod's code.

## 0.5.3 Connected hover and grouping

Connected search now queries the installed game's buildable octree and lightweight
spatial grid, which Building Skills already maintains when moving buildings.
Preview differences retain shared native material replacements instead of spawning
the whole set on every target change. These changes follow the SDK's spatial-query
and customization lifecycle APIs; no additional third-party mod code was used.
The grouping change comes from reviewing our own server revision validation:
explicit building selections are validated directly, while selected containers
still require the client's hierarchy revision to match.

The new movement/search fixture exposed stale native factory-proximity entries
when destroying a moved constructor. The SDK's `AFGBuildable` BeginPlay/EndPlay
registration lifecycle explains the failure. Moves now transfer that entry from
the previous location to the current one on each machine running the transform.

## 0.5.4 copied network references and group synchronization

The installed SDK's `FBlueprintArchiveObjectDataProxy` and
`AFGBuildable::PostSerializedFromBlueprint` define the serialization boundary and
native network rebuilding sequence. Those APIs guided a scoped member filter and
cleanup of foreign references on new network components. The native reciprocal
cleanup behavior motivated clearing only the clone-side pointer before that cleanup
runs. A fresh-world fixture reproduced copied circuits retaining source connections
and placement disconnecting an original boundary pipe; both now pass. No third-party
implementation was copied.

The bounded group snapshot follows the acknowledged transport already used for
Building Skills clipboards and transform snapshots. Native 64-bit network object
identities retain distant members until their actors become relevant. These IDs
are transient and are never added to game saves. This addresses the oversized
replicated group array observed in the supplied server log.

## 0.6.0 Volume and deduplication

Volume follows the existing Connected spatial queries and native Build Gun mode
radial, with the existing modal-input lifecycle for settings. Its asymmetric face
adjustment and center-inclusion rule come directly from the requested behavior.
Deduplication uses the SDK's material recipe mapping, blueprint TOC loading,
construction-message serialization and native dismantling/refund interfaces.
The complete recipe catalog supplies structural equivalence without depending on
unlocks. Native virtual refund and blueprint customization calculations supply
skipped construction costs, including stored contents. A scoped recipe-manager
fallback supplies the live world's recipes during preview-world cost reads.
Native curve arc-length evaluation supplies preview costs before BeginPlay.
Skip prices the remaining archive members and excludes skipped nested refunds;
it does not subtract from headers that can include external source connections.
These integration points were checked
against the installed game binary and isolated native construction fixtures.
No additional third-party implementation was copied for these features.

For 0.6.1, the Volume color preference follows the game's native `UFGUserSetting`
asset, `UFGUserSetting_LinearColorPicker`, UI/Holograms category and
`UFGGameUserSettings` persistence. The SDK's existing hologram option supplied the
category and manager conventions; the new setting asset belongs to Building Skills.
No native widget or third-party implementation was copied.

## Stick spline placement

The native `AFGSplineHologram` family, its routing and connection interfaces,
and `AFGHologram` composite-child construction supplied the integration model.
The installed game's construction-message traversal was inspected to check
dynamic child creation and serialization. These interfaces keep recipe costs
and placement rules in the native pipeline. The camera-distance tool and its
controls come from the requested behavior. The nearest-helix solver and exact
Hermite curve subdivision were implemented here; no third-party mod code was
copied for this feature.

The 0.7.1 placement corrections follow the native validation/reset order,
pipe and belt start-connection checks, and pole floor validation. The native
construction interface also supplies the correctly adjusted C++ pointer used
by the cross-platform serialization hook. This review motivated explicit
initialization of generated segments, a temporary preceding-segment reference
during validation, and a transient Stick permission for floating supports.

## 0.7.2 native snapping and optional rail supports

[Auto Rail Supports](https://github.com/itsalxx-sudo/AutoRailSupports), version
1.1.0, revision `15a6a8ac4aaed18a2179022f19a2938ac8acabaf`, was reviewed under
its [GPL-3.0 license](https://github.com/itsalxx-sudo/AutoRailSupports/blob/15a6a8ac4aaed18a2179022f19a2938ac8acabaf/LICENSE).
Its client manager reads the active rail spline for preview and remote preview;
its server listens to native construction notifications and uses the building
player to choose settings and charge costs. Those integration points motivated
scoped full-route exposure during its client update and retention of the
construction player on dynamic Stick children and during native construction
notifications. No ARS implementation was copied,
vendored or linked into this MIT plugin; integration uses Unreal reflection and
native game interfaces, and the installed ARS plugin remains unchanged.

The installed game's native endpoint snapping supplies candidate selection,
compatibility checks and the configured modifier behavior. Stick now preserves
an accepted native endpoint rather than correcting it back to the virtual tip.
Inspection of native blueprint subsystem shutdown exposed an unchecked Destroy
call on preview-list entries. The defensive cleanup removes expired references
before native shutdown; it does not repair or migrate saves.

The follow-up pillar-spacing review checked the same ARS revision's construction
frame batching, connected-chain traversal, tile-center sampling, per-slot
`EveryN`/offset rules, shared blueprint proxy, and per-stamp material charges.
This motivated regression coverage with fifty under-rail tiles, two every-eighth
pillar slots, and isolated copies of a real preset. These are test improvements;
no additional ARS implementation was copied or changed.

## Action undo and transform polish (0.8.0)

The installed game SDK's native construction and dismantle transactions define
the boundaries of each history action. Native blueprint archives provide building
serialization; supplemental snapshots preserve inventories, blueprint/group
identity and connections across deletion. The existing Copy archive adapter
inspired the scoped in-memory loader, so undo does not create library blueprints
or temporary blueprint files. Rail-signal guarded/observed connection APIs supply
the native ownership relationships used during restoration. No additional external
mod source was copied for this update.

## Construction transformers (0.9.0)

The interaction and reflection rules come from the requested feature design.
Implementation uses Unreal's runtime Slate color wheel and Satisfactory's native
blueprint archive/loading APIs, reusing Building Skills' existing copy isolation
and construction-history hooks. No external mod code was copied for this feature.
