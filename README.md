# Building Skills

A Satisfactory/SML mod for persistent, nested building groups. Its **Edit** mode
shares Normal, Blueprint, and Group selection with the vanilla dismantle gun.
The mod reference is `BuildingSkills`; the development version is `0.1.0`.

## Controls

| Default | Action |
| --- | --- |
| Alt + E | Open / Close Edit Mode |
| Ctrl (existing multi-dismantle binding) | Hold and aim to select multiple items |
| R (existing Build Gun mode binding) | Switch Normal / Blueprint / Group selection |
| G | Group |
| M | Merge |
| U | Ungroup, available for exactly one selected group |

The four Building Skills commands can be changed in the game's keybinding settings.
Multi-select and selection-mode switching use the existing game bindings. Group,
Merge, and Ungroup are active only in Edit; M retains its usual Map action elsewhere.
The HUD displays the current bindings, selection mode, and selection counts.

Selection persists when switching modes, including multiple blueprints and groups.
Hold multi-select and sweep over targets to add them. Begin the gesture on an
already selected item to remove items. A pinned selection excludes the unrelated
object under the cursor; when nothing is pinned, commands use the highlighted target.
Closing the gun clears the local selection. Groups themselves persist in the save.

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
keep their original classes, transforms, inventories, connections, and blueprint
membership. Group mutations are validated on the server; all peers need the mod.
Moving, rotating, scaling, group renaming, and skill progression are not implemented.

Actor-backed buildings use game object references. Lightweight buildings are resolved
by class, recipe, and transform, with uniqueness checks; runtime array indices are
not saved. Exact overlapping copies with the same fingerprint are ambiguous and
cannot be grouped. Deleting a member removes its membership and prunes empty groups.

Preserving vanilla buildings after removing the mod is a design target, not yet a
verified compatibility guarantee. See [verification](docs/TESTING.md) for save/load,
multiplayer, and uninstall checks. Saving without the mod should not be expected to
preserve the group hierarchy for later reinstalls.

## Development

- `mod/BuildingSkills/`: Unreal plugin, C++ source, config and committed content.
- `scripts/Mod.ps1`: setup, editor build, assets, tests, packaging and installation.
- [Setup](docs/SETUP.md): toolchain and commands.
- [Verification](docs/TESTING.md): completed checks and in-game test matrix.
- [Grouping implementation](docs/GROUPING.md): hierarchy and integration details.

The plugin is junctioned into the shared SML project's `Mods/GameFeatures` folder.
Generated files and build products are ignored. The project targets CSS Unreal 5.6.1
and SML 3.12.0, declaring game CL 502094 as its minimum.

## Codex participation

OpenAI Codex assisted with plugin setup, Edit input and HUD, nested grouping,
selection integration, build and asset scripts, and automated validation.
Participation is recorded here rather than in commit messages or co-author trailers.
See [verification](docs/TESTING.md) for what was actually run.

## License

MIT — see [LICENSE](LICENSE).
