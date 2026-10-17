# Building Skills

A Satisfactory/SML mod for persistent, nested building groups and building transforms. Its **Edit** mode
shares Normal, Blueprint, Group, Connected and Volume selection with the vanilla dismantle gun.
The mod reference is `BuildingSkills`; the development version is `0.9.7`.

## Controls

| Default | Action |
| --- | --- |
| Alt + E | Open / Close Edit Mode |
| Ctrl (existing multi-dismantle binding) | Hold and aim to select multiple items |
| R (existing Build Gun mode binding) | Switch Normal / Blueprint / Group / Connected / Volume selection |
| K (in Connected selection/painting) | Open Connected Components settings |
| R (in Customizer) | Toggle spread: Default / Connected |
| G | Group |
| M | Merge |
| U | Ungroup, available for exactly one selected group |
| T | Enter Transform with the current selection |
| Ctrl + Z (in Build / Dismantle) | Undo the last construction / deletion action |
| K (in Transform) | Edit temporary movement, rotation and scale steps |
| Alt + S | Save the current selection as a blueprint |
| Alt + C | Copy the current selection into a Build Gun hologram |
| Alt + A | Set / clear the pointed building as the blueprint/copy anchor |
| Alt + V (in Volume) | Switch sphere / cube |
| Scroll (in Volume) | Grow / shrink the sphere or the cube face toward the view |
| Alt + Scroll (in Volume) | Adjust the opposite cube face |
| Shift + Scroll (in Volume) | Rotate the cube horizontally |
| K (in Volume) | Set size and rotation steps |
| Alt + D (with a blueprint hologram) | Toggle deduplication: Off / Replace / Skip |
| Alt + S (with a spline hologram) | Toggle Stick: Off / Default / Constrained |
| Scroll (while Stick is active) | Increase / decrease the stick distance |
| Alt + Scroll (while Stick is active) | Tilt the free endpoint up / down |
| K (while Stick is active) | Set steps, slope and radius; enable each constraint independently |

The Building Skills commands can be changed in the game's keybinding settings.
Stick begins at 64 m, with a 1 m distance step and a 15 degree endpoint-tilt step. Its constrained path uses a fixed
upward slope and signed horizontal radius, and can follow multiple spiral turns.
The K menu lets you enable horizontal and vertical constraints independently, retaining disabled values. With both disabled, placement uses native routing.
Long paths are split into connected segments. Stick settings last until leaving
the save; persistent defaults are in mod configuration. [Details](docs/STICK.md).
Transform's K menu accepts typed distances in cm, angles in degrees and scale
multipliers for every step tier. Closing the window retains these temporary values;
closing Edit restores the values from mod configuration.
Multi-select and selection-mode switching use the existing game bindings. Group,
Merge, and Ungroup are active only in Edit; M retains its usual Map action elsewhere.
The left panel displays selection mode and counts. Edit commands use the native
bottom hint bar, and the bottom-right shortcut helper includes the Edit binding.
Edit highlights and its status panel are green; dismantle retains its native highlight style,
including the hovered target after other buildings have been selected.

Selection persists when switching modes, including multiple blueprints and groups.
Hold multi-select and sweep over targets to add them. Begin the gesture on an
already selected item to remove items. A pinned selection excludes the unrelated
object under the cursor; when nothing is pinned, commands use the highlighted target.
Closing the gun clears the local selection. Groups themselves persist in the save; empty groups are removed when their last member is deleted.

Build and Dismantle keep separate, per-player undo histories, with a configurable
limit measured in actions (default 32). Restoring a deletion takes back its refund
and restores contents, groups, blueprint membership and available connections.
These histories last for the session. [Details](docs/UNDO.md).

Transform defaults to Local axes; World and the Alt+A Anchor axes are also available.
The selected Transform pivot is called the Reference. For one object or a complete
blueprint, the left panel shows position, rotation and scale, relative to the Alt+A
anchor when present.

**Connected Components** expands from the pointed building through touching
bounding boxes, with a configurable gap, object limit, type/category scope,
material/swatch matching and name regex filters. Connected selection is shared
by Edit and Dismantle; Connected painting applies a material or swatch to the
matching set and previews the changes before applying them. The default limit
is 64 objects. [Details and filter behavior](docs/CONNECTED.md).

**Volume** selects building centers inside a sphere or adjustable cube around the
player or anchor. It supports the same persistent multi-selection in Edit and
Dismantle. [Controls and bounds](docs/VOLUME.md).

**Blueprint deduplication** compares structural building types, centers and oriented
bounds, ignoring material and swatch. Replace dismantles existing matches with
native refunds after successful placement; Skip keeps them and omits duplicates
from the new placement. It defaults to Off and is active only with a blueprint
hologram, including copied selections.

**Save** opens the native blueprint details window for the current selection,
including individual buildings, multiple groups and nested group members. Choose its name, description, icon and category,
then save a normal game blueprint. Load and Clear are unavailable in this window.
Saving leaves the original buildings and groups in place. Plain S and C keep their
movement actions even when buildings are selected.
Saved selections align horizontally with the majority of their buildings. Their origin
is centered on X/Y, with Z=0 at the bottom of the contained geometry.

**Copy** opens a placement hologram without creating a blueprint in the library.
Copies retain selected groups and their nesting with independent group identities.
Alt+C is configurable and appears beside Save when a selection is available.

**Alt+A** sets the pointed building as the Save/Copy anchor; repeat on that building
to clear it. The anchor is blue when excluded, purple when also selected with Ctrl.
With an anchor, select the desired contents explicitly. Its rotation defines all
three box axes. An excluded anchor can lower the box's floor but does not enlarge
its X/Y bounds or become saved content. The origin remains the bottom center in
that frame. With no anchor, automatic majority alignment remains unchanged.

Blueprint saving preserves building scales in the existing native blueprint field.
Building Skills restores those scales in previews and placement; a vanilla game can
read the file but places its buildings at normal size. This also applies to saving
from ordinary blueprint designers. See [blueprint behavior](docs/BLUEPRINTS.md) and
[verification status](docs/TESTING.md).

- **Group** creates a new group containing the selected individual buildings and
  selected groups. Existing groups become children of the new group.
- **Merge** adds buildings to the first selected group. With multiple selected
  groups, it dissolves their outer containers into that group and preserves their
  nested child groups.
- **Ungroup** removes one selected outer container, promoting its buildings and
  child groups one level. It does not recursively dissolve the hierarchy.
- **Blueprint** selection contributes individual buildings to grouping operations;
  vanilla blueprint membership remains unchanged.
- **Group** selection targets the outermost group containing the aimed building.
  After ungrouping it, its child groups can be selected independently.

The same persistent selection and Group mode are available in vanilla dismantle
mode, which uses the game's normal dismantle/refund pipeline. Left click in Edit
cannot dismantle buildings. Selection is limited to 2048 buildings per operation;
selections above that limit are rejected rather than partially processed.

## Save data and compatibility

Groups are optional metadata owned by a saved, replicated mod subsystem. Buildings
keep their native classes, inventories and blueprint membership. Transform updates
the native saved transforms directly. Mutations are validated on the server; all
peers need the mod. Group renaming and skill progression are not implemented.

The mod's own world-save fields contain only the group hierarchy, member references,
pivots and a schema version. Building positions, rotations, scales and edited network
geometry live in native building records. Selection, undo/redo history, replication
snapshots, copy clipboards and blueprint Save sessions are transient. Blueprints are separate files;
scale preservation adds no world-save fields or companion blueprint files.

Actor-backed buildings use game object references. Lightweight buildings are resolved
by class, recipe, and transform, with uniqueness checks; runtime array indices are
not saved. Exact overlapping copies with the same fingerprint are ambiguous and
cannot be grouped. Deleting a member removes its membership and prunes empty groups.

Preserving vanilla buildings after removing the mod is a design target, not yet a
verified compatibility guarantee. See [verification](docs/TESTING.md) for save/load,
multiplayer, and uninstall checks. Saving without the mod should not be expected to
preserve the group hierarchy for later reinstalls.

## Transform

Press **T** in Edit to hold the selection fixed and transform it. **R** cycles
Rotate X / Y / Z, then Scale XYZ / XY / XZ / YZ / X / Y / Z. Scroll to apply the
active operation; the colored axis display marks the active axes at the shared pivot.
Arrow keys move relative to your facing; Page Up / Down move vertically. **Esc**
returns to normal Edit, keeping the changes and selection.

**P** changes the pivot, **L** switches world/local axes, and **B** changes the
anchor building. **Z / Y** undo/redo transforms, including affected connections.
Cyan wire boxes preview queued movement while the server applies it.

Movement bindings, these transform controls, T, and the three held step modifiers are rebindable. Step amounts
and repeat delay are in **Mods > Building Skills**. See [Transform](docs/TRANSFORM.md)
for defaults and scale semantics. Connected buildings retain their links. Belts,
pipes and rails at the selection boundary stretch toward fixed, unselected endpoints;
power wires follow their connection points. Extractors keep their resource bindings.

## Development

- `mod/BuildingSkills/`: Unreal plugin, C++ source, config and committed content.
- `scripts/Mod.ps1`: setup, editor build, assets, tests, packaging and installation.
- [Setup](docs/SETUP.md): toolchain and commands.
- [Verification](docs/TESTING.md): completed checks and in-game test matrix.
- [Grouping implementation](docs/GROUPING.md): hierarchy and integration details.
- [Build and Dismantle undo](docs/UNDO.md): action history, refunds and restoration checks.
- [Transformers](docs/TRANSFORMERS.md): ordered mirror/rotation copies, live previews,
  and matching deletion, paint and transform actions.
- [Blueprint saving](docs/BLUEPRINTS.md): saving/copying selections and online checks.

The plugin is junctioned into the shared SML project's `Mods/GameFeatures` folder.
Generated files and build products are ignored. The project targets CSS Unreal 5.6.1
and SML 3.12.0, declaring game CL 502094 as its minimum.

## Codex participation

OpenAI Codex assisted with plugin setup, Edit input and HUD, nested grouping,
selection integration, build and asset scripts, installation verification, and
automated validation in both the editor and the packaged game. Follow-up work
covered native hint layout, the shortcut helper, refund-update batching, cached
lightweight lookup, packaged group-hover regression checks, transform controls,
native transform updates, the axis display, transform validation, connected-network
geometry updates, specialized-building regression checks, group blueprint saving,
uniform XYZ scaling, Linux server packaging, multiplayer save-window handoff fixes,
restoring spline collision registration after transforms, transform history,
configurable pivots and frames, queued-input previews, batched late-join geometry,
client blueprint save forwarding, independent hover outlines, blueprint scale and
alignment, a licensed-source feature review with hierarchy and input-queue fixes,
pooled machine-visual refresh, selection-based Save and clipboard hologram copying,
Connected and Volume selection, blueprint deduplication, Stick spline routing,
endpoint tilt and their packaged regressions, per-player construction/deletion undo,
empty-group cleanup, anchor-relative pose readouts, frame-relative movement,
batched native compilation, and session-only construction transformers with
ordered mirror/rotation copies, color controls and live hologram previews,
editable world/anchor coordinates, placement blueprint identity and matching
deletion/material/swatch operations, plus repeated Edit transforms with shared
connection updates and undo/redo, input/output-aware mirror orientation, and
lift deletion archive safeguards, bounded selection uploads and replies, and
large-selection regression coverage, split-rail endpoint connection checks,
and blueprint conveyor-chain isolation with an imported-blueprint regression.
Participation is recorded here rather than in commit messages or co-author trailers.
See [verification](docs/TESTING.md) for what was actually run.

## License

MIT — see [LICENSE](LICENSE).
