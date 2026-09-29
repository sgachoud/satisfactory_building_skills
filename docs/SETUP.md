# Development setup

Building Skills uses the same separate-repository/junction workflow as
`F:/dev/satisfactory_mods/view_throught_collectibles`.

| Component | Local configuration |
| --- | --- |
| Plugin | `F:/dev/satisfactory_mods/building_skills/mod/BuildingSkills` |
| SML project | `F:/dev/satisfactory_mods/SatisfactoryModLoader/FactoryGame.uproject` |
| Engine | `C:/Program Files/Unreal Engine - CSS` (5.6.1-CSS) |
| SML | 3.12.0 |
| Declared minimum game build | CL 502094, matching the reference mod |
| Compiler | VS 2022 / MSVC v14.38 x64/x86, per the reference setup |
| Audio SDK | Wwise 2023.1.14.8770, per the reference setup |

Use the installed checkout as the source of truth for this workflow. The
[official guide](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/SimpleMod/gameworldmodule.html)
provides general plugin and root-module background, but may describe another SML version.

## Initial setup

Run in this repository with Unreal Editor closed:

```powershell
.\scripts\Mod.ps1 Link
.\scripts\Mod.ps1 ProjectFiles
.\scripts\Mod.ps1 BuildEditor
.\scripts\Mod.ps1 InitContent
```

`Link` creates `<SML>/Mods/GameFeatures/BuildingSkills` pointing at the entire plugin
folder. It accepts an existing correct junction and refuses to replace anything else.
`ProjectFiles` refreshes the shared Visual Studio solution. `BuildEditor` builds
FactoryEditor Win64 Development. These actions write into the shared SML checkout
and engine/user caches as well as this repository, so a sandbox may request permission.

`InitContent` uses the engine's Python commandlet to create missing game-feature data
and empty SML root Blueprints. The feature's scan rules come from the installed
Alpakit template. Existing assets are preserved. Run this only when bootstrapping a
checkout with missing assets; normal development edits the committed assets in Unreal.

All actions accept `-SmlRoot` and `-EngineRoot` to use another installation.
Open `FactoryGame.uproject` in the CSS editor after the build. Enable **Show Plugin
Content** in the Content Browser and select **Building Skills Content**. Alpakit Dev
should list the mod; select Building Skills when packaging from the editor.

## Content and code

- `/BuildingSkills/BuildingSkills`: GameFeatureData with the standard Alpakit scan rules.
- `/BuildingSkills/RootGameInstance_BuildingSkills`: root SML GameInstanceModule
  Blueprint registering the grouping remote call object.
- `/BuildingSkills/RootGameWorld_BuildingSkills`: root SML GameWorldModule Blueprint
  registering the saved, replicated grouping subsystem.
- `Source/BuildingSkills`: native runtime module and `LogBuildingSkills` log category.

Compile and save modified Blueprints. Commit `.uasset` files. Close the editor before
structural C++ changes and run `BuildEditor` again. The small native module disables
shared precompiled headers, matching the reference project's build optimization.

## Package

For Edit-mode development, build the editor first. The input assets are committed;
to regenerate their definitions deliberately, run `scripts/Mod.ps1 EditAssets`.
This updates the mod-owned input assets and the two root module registrations. The
Edit toggle uses the vanilla Alt chord and player-actions parent context. A dedicated
Edit context with Group/Merge/Ungroup mappings is bound directly only while Edit is
open. The game feature scans this directory for child input mapping contexts.
Run `scripts/Mod.ps1 Test` for hierarchy, save-property and input regression
coverage; reports are written under the repository's ignored `Saved/Automation` folder.

```powershell
# Windows clients (Steam and Epic); no deployment or game launch.
.\scripts\Mod.ps1 Package

# Combined Windows client, Windows Server and Linux Server release archive.
.\scripts\Mod.ps1 Release

# Build, package and install with Alpakit; close Satisfactory first.
.\scripts\Mod.ps1 Install

# Recheck every installed package file against the archive by SHA-256.
.\scripts\VerifyInstall.ps1

# Exercise the installed game in a fresh temporary world; saving is blocked.
.\scripts\RuntimeTest.ps1 -Render
```

Build the editor and save content first. Linux packaging requires the matching
Unreal cross-compilation toolchain. Alpakit writes archives under
`<SML>/Saved/ArchivedPlugins/BuildingSkills`; the merged release upload is
`BuildingSkills/BuildingSkills.zip` beneath `ArchivedPlugins`.

Install also runs the SHA-256 check and writes `Saved/InstallationVerification.json`.
The runtime check requires Satisfactory to be closed. It exits automatically and
writes `Saved/RuntimeTest.log`; `-Render` also retains `Saved/RuntimeTest.png`.
It does not load an existing save and only installs its save-blocking hook when the
dedicated runtime-test command-line flag is present.

Deployment can be configured in Alpakit for the existing local game installation:
`F:/Programme et jeux/Steam/steamapps/common/Satisfactory`. Run the smoke test in
[TESTING.md](TESTING.md) before distributing a build.

Before publishing, set real repository/support URLs, add a mod icon, confirm tested
game/platform compatibility and review the multiplayer requirement. Keep `VersionName`
equal to `SemVersion`, and integer `Version` equal to its major component (`0` for
`0.1.0`), following the reference project's release convention.

For `LNK1181` mentioning `delayimp.lib`, check that MSVC v14.38 **x64/x86** tools are
installed; the ARM64 package alone is insufficient.

The wrapper also gives child build processes the Windows PowerShell module path.
This avoids a Wwise pre-build hook failure when Unreal inherits PowerShell 7 module
paths through `cmd.exe` (`Microsoft.PowerShell.Security` fails to load). The original
environment is restored when the command completes; no system policy is changed.

The build wrapper defaults to two compiler workers to keep shared precompiled-header
memory use bounded. Override with `-MaxParallelActions` if appropriate for another
machine. An initial shared-header rebuild can be much larger than a mod-only rebuild.


### Isolated native compilation

`BuildEditor`, Windows `Package`, and `Install` use `scripts/BuildNative.ps1` against
this workflow's existing SDK binaries. UBT prepares reflection/response files with
`-SkipBuild`; the script compiles only Building Skills and links in dependency order.
This avoids rebuilding the SDK's large shared-PCH stubs after an access-friend change.
It requires the documented, already-built SDK and pinned MSVC toolchain; it is not a
replacement for building/upgrading the SDK itself. No SDK source is edited.

If an interrupted full build removed SDK import-library caches, the script derives
temporary imports from its existing cached objects under this repository's
`Saved/NativeBuild`. It does not write those libraries into the shared checkout.
Alpakit then cooks, packages and installs the new DLL. `-SkipNativeBuild` on `Install`
is only for immediately reusing an already successful native build during iteration.
The broader `Release` path still uses the standard full target build and is untested
for the Transform feature.
