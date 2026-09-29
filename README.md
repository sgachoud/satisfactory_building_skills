# Building Skills

A Satisfactory/SML mod adding an **Edit** mode entry point to the Build Gun.
The first feature opens and closes the mode; building-editing operations and skill
progression are not implemented yet.

The display name is **Building Skills**, the mod reference is `BuildingSkills`,
and the initial development version is `0.1.0`.

## Edit mode

Press **Alt + E** during normal gameplay to open the Build Gun in Edit mode. Press
the shortcut again to close it. An **EDIT** indicator shows the current shortcut.
The binding is listed as **Open / Close Edit Mode** under **Building Skills** in the
game's keybinding settings.

Edit starts with no construction recipe or hologram. Switching to normal building,
dismantling or painting leaves Edit mode. The shortcut uses the game's chord system
to distinguish it from plain E, and is unavailable in interaction menus, while paused,
in vehicles, while dead or in portals.

## Development

The repository contains:

- `mod/BuildingSkills/`: Unreal plugin, C++ source, config and committed content.
- `scripts/Mod.ps1`: setup, editor build, asset generation, tests and Alpakit packaging.
- [Setup](docs/SETUP.md): shared SML project, toolchain and commands.
- [Verification](docs/TESTING.md): validation results and packaged-game smoke test.

The plugin is junctioned into the shared SML checkout's `Mods/GameFeatures` folder.
Generated files and build products are ignored. The committed game-feature data and
empty game-instance/game-world root Blueprints are the starting points for future
content and configuration. The native module logs `Building Skills module loaded.`

This scaffold targets the installed CSS Unreal 5.6.1 / SML 3.12.0 setup, with game
CL 502094 as its declared minimum. These declarations are not a claim of testing
against newer game builds. Both peers are required to install the mod for now;
revisit `RequiredOnRemote` when the actual multiplayer design is defined.

## Codex participation

OpenAI Codex assisted with the plugin setup, Edit-mode input and HUD implementation,
build and asset scripts, and validation. Participation is recorded here rather than
in commit messages or co-author trailers. See [verification](docs/TESTING.md) for
what was actually run.

## License

MIT — see [LICENSE](LICENSE).
