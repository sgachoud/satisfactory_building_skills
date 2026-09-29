# Verification

## Setup validation

Completed on 2026-09-29 with CSS Unreal 5.6.1 and SML 3.12.0:

- The plugin junction resolves to this repository's `mod/BuildingSkills` folder.
- Visual Studio project generation succeeded. It warned about the installed engine's
  missing SVN files when inspecting the generic UnrealEditor target; FactoryEditor
  project generation and the actual FactoryEditor build succeeded.
- `BuildEditor`: FactoryEditor Win64 Development compiled and linked successfully
  (81.79 seconds). The build used MSVC 14.38 and Windows SDK 10.0.22621.0.
- `InitContent`: a fresh editor process loaded the native module, activated the
  BuildingSkills game feature and verified both saved Blueprint root flags and parent
  classes. Final commandlet result: success, zero errors, 30 warnings. Warnings include
  shared-project Python binding metadata and junction path capitalization diagnostics.
- PowerShell parsing and plugin identity/version consistency checks passed.
- Git ignores generated plugin binaries/intermediates and the local validation logs.

The first asset creation pass reported the expected missing game-feature asset during
startup, before Python created it. The bootstrap wrapper now requires a clean restart
after that case. Asset discovery is synchronized before checking for existing assets.
The final verification above used the saved assets in a new Unreal process.

Logs are local and ignored: `Saved/InitContent.log`, `Saved/Package.log`, and Unreal's
build log at `%LOCALAPPDATA%/UnrealBuildTool/Log.txt`.

`Package` completed successfully for Windows (Steam and Epic Shipping binaries) in
1 minute 30 seconds. The archive is
`F:/dev/satisfactory_mods/SatisfactoryModLoader/Saved/ArchivedPlugins/BuildingSkills/BuildingSkills-Windows.zip`.
The three starter content packages were cooked. No deployment or in-game smoke test
has run. Windows Server, Linux Server and the combined `Release` action are untested.

## Packaged-game smoke test

1. Package with Alpakit and copy the build into a development game installation.
2. Launch the game and check that **Building Skills 0.1.0** appears in the mod list.
3. Confirm `LogBuildingSkills: Building Skills module loaded.` appears in the log.
4. Load a test save, save/reload, and return to the menu. Check for errors naming
   BuildingSkills or its root modules. The scaffold should make no gameplay changes.
5. Test host/client and the intended dedicated-server platforms once multiplayer
   behavior is implemented. The initial descriptor requires installation on both peers.

The shared modding project's FactoryGame implementations are stubs. Editor compilation
and asset checks cannot establish packaged-game behavior. Add focused automated tests
when gameplay logic exists; there is no gameplay test suite for this empty scaffold.
