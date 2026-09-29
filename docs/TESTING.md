# Verification

## Edit-mode validation

On 2026-09-29, the Edit-mode code compiled and linked for FactoryEditor Win64
Development. Input asset generation completed with zero errors. The
`BuildingSkills.Input.EditChord` automation test passed, exercising Unreal's actual
mapping rebuild, input evaluation and chord-blocker injection with the saved assets:

- Plain E triggers the ordinary action and does not trigger Edit.
- Alt+E triggers Edit and suppresses the competing E action, even when that other
  context has higher priority.
- Holding the chord keeps the competing action blocked.
- Releasing E stops the shortcut, and ordinary E works after the chord.
- Removing the modifier through the engine's remapping mechanism removes the Alt
  requirement; the default chord is not hard-coded into the C++ callback.

Run with `scripts/Mod.ps1 Test`. Reports are in `Saved/Automation` relative to this
repository. This test does not execute the stubbed FactoryGame equipment methods or
verify the rendered HUD.

Windows packaging also passed for Steam and Epic Shipping targets (1 minute 50 seconds).
All five mod content assets were cooked, including the input action and child context.
The updated archive is `Saved/ArchivedPlugins/BuildingSkills/BuildingSkills-Windows.zip`
relative to the shared SML project root. It has not been deployed or tested in-game.

## Initial setup validation

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
1 minute 30 seconds. The archive is at
`Saved/ArchivedPlugins/BuildingSkills/BuildingSkills-Windows.zip`, relative to the
shared SML project root.
The three starter content packages were cooked. No deployment or in-game smoke test
has run. Windows Server, Linux Server and the combined `Release` action are untested.

## Packaged-game smoke test

1. Package with Alpakit and copy the build into a development game installation.
2. Launch the game and check that **Building Skills 0.1.0** appears in the mod list.
3. Confirm `LogBuildingSkills: Building Skills module loaded.` appears in the log.
4. On foot, press Alt+E: the Build Gun should appear with an EDIT indicator and no
   construction hologram. Holding it must not repeatedly open/close the mode. Press
   it again to close. Check both starting with the gun holstered and starting in
   build, dismantle or paint mode.
5. Stand by an interactable machine. Plain E must still open it; Alt+E must open Edit
   without also interacting or quick-switching. Check both key-release orders.
6. Change **Open / Close Edit Mode** under **Building Skills** in the keybinding
   settings, including changing the modifier and binding a single key. Confirm the
   action and its on-screen hint follow the new binding and persist after restarting.
7. Switch to normal build/dismantle/paint, select a hotbar recipe or sample a building.
   Edit's indicator should disappear and vanilla behavior should work. Left click
   in empty Edit mode must neither build nor dismantle anything.
8. Check interaction menus, pause, vehicles, death/respawn, portals, save reload and
   reconnect. The shortcut must not open Edit in restricted contexts; no orphaned
   indicator or duplicate input callbacks should remain afterward.
9. Repeat with a listen host and remote client before claiming multiplayer support.
   Server platforms also need packaging and smoke tests. The descriptor requires
   installation on both peers.

The shared modding project's FactoryGame implementations are stubs. Editor compilation
and the input regression test cannot establish packaged-game equipment behavior.
Building-editing operations are not part of this first feature.
