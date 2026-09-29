# Building Skills

A new Satisfactory/SML mod. This repository currently provides the development
foundation; building skills, progression and other gameplay features are not yet
implemented.

The display name is **Building Skills**, the mod reference is `BuildingSkills`,
and the initial development version is `0.1.0`.

## Development

The layout follows the local View Through Collectibles workflow:

- `mod/BuildingSkills/`: Unreal plugin, C++ source, config and committed content.
- `scripts/Mod.ps1`: link, editor build, content initialization and Alpakit packaging.
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

OpenAI Codex assisted with inspecting the existing development workflow, scaffolding
the plugin, preparing build and asset-initialization scripts, and documenting
validation. Participation is recorded here rather than in commit messages or
co-author trailers. See [verification](docs/TESTING.md) for what was actually run.

## License

MIT — see [LICENSE](LICENSE).
