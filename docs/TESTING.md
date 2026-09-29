# Verification

## Grouping validation

Completed on 2026-09-29 with CSS Unreal 5.6.1 / SML 3.12.0:

- FactoryEditor Win64 Development compiled and linked successfully, including
  native-hook signature checks. Final build: 49.04 seconds.
- Input assets and root module registrations were generated successfully: zero errors.
- All four automation tests passed with no test warnings or failures:
  `Groups.NestMergeUngroup`, `Groups.MergePreservesChildren`,
  `Groups.MetadataSaveRoundTrip`, and `Input.EditChord` under `BuildingSkills`.
- PowerShell parsing, diff whitespace, and documentation privacy checks passed.

Steam Win64 Shipping packaging and installation passed (12 minutes 7 seconds,
including a shared-header rebuild). All ten content packages were cooked. Alpakit
installed the mod under the game's `FactoryGame/Mods/GameFeatures/BuildingSkills`
folder. All seven installed package files matched the archive by SHA-256.
The archive is `Saved/ArchivedPlugins/BuildingSkills/BuildingSkills-Windows.zip`
relative to the shared SML project; the local verification receipt is
`Saved/InstallationVerification.json` relative to this repository.

The final editor build and all four tests were rerun successfully after the selection
lifecycle fixes. The initial packaged build crashed during game startup: the
`Server_DismantleActors_Implementation` RPC hook used `SUBSCRIBE_METHOD`, which
cannot resolve a virtual implementation without a sample object. It now uses
`SUBSCRIBE_UOBJECT_METHOD`, supplying the class default object to SML.
Editor tests did not catch this because native hooks are disabled against editor stubs.

Crash-fix verification on 2026-09-29:

- FactoryEditor rebuilt successfully (41.53 seconds).
- Steam Shipping rebuilt, packaged and installed successfully (1 minute 34 seconds).
- All seven installed files matched the rebuilt archive by SHA-256.
- The installed Shipping executable ran with `-nullrhi -nosound -unattended
  -nosplash -NO_EOS_OVERLAY -ExecCmds=quit`. All ten Building Skills native hooks
  registered, engine initialization completed, and the process exited with code 0.
  The log is `Saved/CrashFix-Startup.log` relative to this repository. This checks
  packaged startup only; it does not load a save or exercise selection/grouping.

In-game behavior, actual game save/load,
multiplayer and uninstall compatibility remain unverified. This grouping build has
not been packaged for Epic, Windows Server or Linux Server.

Run `scripts/Mod.ps1 BuildEditor`, `scripts/Mod.ps1 EditAssets`, and
`scripts/Mod.ps1 Test`. Reports are in `Saved/Automation` relative to this repository;
logs are in the ignored `Saved` folder. FactoryGame implementations in the editor
checkout are stubs, so native hooks run only in packaged game builds.

Automation covers:

- Grouping individual buildings, nesting groups with singles, and deduplication.
- Merge into an existing group, merging multiple groups, and retaining nested children.
- Ungrouping exactly one level; rejecting multiple-group ungroup selections.
- Individual extraction from an existing group, common parents, and cycle rejection.
- Save-property serialization of IDs, parents, names and lightweight fingerprints;
  runtime lightweight indices must not be serialized.
- Alt+E chord behavior, input remapping of modifiers, rebindable G/M/U actions,
  and the M/Map conflict being scoped to the Edit context.

## In-game test matrix

Use a separate test save containing machines, foundations and two placed blueprints.

1. Open Edit with Alt+E. Hold the configured multi-dismantle key (default Ctrl) and
   aim across two machines and two foundations. Release it and press Group (G).
   Confirm four buildings are highlighted, one group is selected, and left click
   cannot dismantle anything in Edit.
2. Switch with the configured mode key (default R) through Normal, Blueprint and
   Group. Previously selected items must remain selected. The HUD must follow both
   the mode and the configured key names.
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
   Dismantle a nested group and verify all selected descendants disappear, materials
   are refunded once, and unrelated buildings remain. Check cancelled dismantles,
   disqualified buildings, full inventory, and lightweight foundations/walls.
9. Rebind the existing multi-dismantle and Build Gun mode actions, and all four
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
    platforms also need build and gameplay checks before claiming support.
13. On a copy of the save, remove the mod, load, save again and reload. Compare base
    building counts, transforms, inventories, recipes and connections. Group metadata
    is expected to be unavailable; do not expect reinstalling to recover it from a
    save written without the mod.

Move/rotate/scale are not implemented in this iteration. Their compatibility checks
from the design document apply when those operations are added.

## Earlier entry-point checks

Before grouping was added, FactoryEditor compilation, input asset generation,
`BuildingSkills.Input.EditChord`, and Steam/Epic Windows packaging passed on
2026-09-29. Those results covered the original Edit entry point, not the new selection
and grouping implementation. Initial project/junction generation and root asset
registration also passed. The editor warns about some shared-project Python metadata
and junction capitalization; those are not gameplay validation.
