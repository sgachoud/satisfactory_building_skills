# Building Skills

A Satisfactory/SML mod for persistent, nested building groups and building transforms. Its **Edit** mode
shares Normal, Blueprint, and Group selection with the vanilla dismantle gun.
The mod reference is `BuildingSkills`; the development version is `0.3.3`.

## Controls

| Default | Action |
| --- | --- |
| Alt + E | Open / Close Edit Mode |
| Ctrl (existing multi-dismantle binding) | Hold and aim to select multiple items |
| R (existing Build Gun mode binding) | Switch Normal / Blueprint / Group selection |
| G | Group |
| M | Merge |
| U | Ungroup, available for exactly one selected group |
| T | Enter Transform with the current selection |
| S | Save as blueprint, available for exactly one selected group |

The Building Skills commands can be changed in the game's keybinding settings.
Multi-select and selection-mode switching use the existing game bindings. Group,
Merge, and Ungroup are active only in Edit; M retains its usual Map action elsewhere.
The left panel displays selection mode and counts. Edit commands use the native
bottom hint bar, and the bottom-right shortcut helper includes the Edit binding.

Selection persists when switching modes, including multiple blueprints and groups.
Hold multi-select and sweep over targets to add them. Begin the gesture on an
already selected item to remove items. A pinned selection excludes the unrelated
object under the cursor; when nothing is pinned, commands use the highlighted target.
Closing the gun clears the local selection. Groups themselves persist in the save.

**Save** opens the native blueprint details window for the selected group's complete
contents, including nested groups. Choose its name, description, icon and category,
then save a normal game blueprint. Load and Clear are unavailable in this window.
Saving leaves the original buildings and group in place. S remains a movement key
when the Save action is unavailable.

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

Movement bindings, T, and the three held step modifiers are rebindable. Step amounts
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
- [Blueprint saving](docs/BLUEPRINTS.md): saving a group and online checks.

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
and restoring spline collision registration after transforms.
Participation is recorded here rather than in commit messages or co-author trailers.
See [verification](docs/TESTING.md) for what was actually run.

## License

MIT — see [LICENSE](LICENSE).
