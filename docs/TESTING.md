# Verification

Validated on 2026-09-29 with CSS Unreal 5.6.1, SML 3.12.0 and the installed Steam
Windows game, CL 502094. Current package: Building Skills 0.2.0.

## Completed checks

- FactoryEditor Win64 Development compiled and linked successfully.
- Input assets and the shortcut widget-hook registration were generated with zero errors.
- All five editor automation tests passed without test warnings or failures:
  `Groups.NestMergeUngroup`, `Groups.MergePreservesChildren`,
  `Groups.MetadataSaveRoundTrip`, `Input.EditChord`, and `Transform.PivotAndInverse`
  under `BuildingSkills`.
- Steam Win64 Shipping packaging and installation succeeded. All seven installed
  files matched the archive by SHA-256. `Install` now performs this verification
  automatically and records `Saved/InstallationVerification.json`.
- The installed Shipping game passed all 47 runtime assertions in a fresh temporary
  world. The test injects key events into the game's player input and uses the native
  mode-cycle API. It verifies Alt+E opening/closing, visible Edit HUD, building
  targeting, Ctrl multi-selection, selection persistence through Edit's Normal,
  Blueprint and Group modes, G grouping through the server request, M merging a
  single into a group, U ungrouping, and Edit's dismantle protection.
- The same run opens vanilla dismantle with F, selects multiple buildings, cycles
  its available modes, holds the dismantle input, and verifies that both selected
  buildings disappear while the unselected building survives.
- The group-hover fixture adds 128 native lightweight foundations to an actor-backed
  machine. It checks group expansion when aiming at the machine and at a foundation,
  stable selection/refund refresh counters, and bounded UI-widget counts. Stable
  hover recorded zero selection rebuilds, zero refund refreshes and zero new UI
  widgets. Vanilla dismantle is also checked against the 129-building group.
- Lightweight lookup checks add an identical overlapping foundation, remove the
  overlap, delete the original, and add a replacement elsewhere. Ambiguous matches
  are rejected and reused slots never become the original building.
- The rendered screenshot was inspected: the EDIT status panel is on the left,
  configured controls use native hint widgets at the bottom, and Alt+E / Edit sits
  inside the bottom-right shortcut grid. The native dismantle-only overlay is hidden
  during Edit and restored for dismantling.

- Transform checks enter with T, inject the native R and mouse-wheel bindings,
  and rotate a group containing one storage container and two lightweight foundations.
  All ten modes and their inverse steps restore the original native transforms.
  Invalid scaling is rejected without changing the hierarchy revision.
- Held Ctrl plus Up moves that group along the camera-relative axis with the Tiny
  step. Page Up moves along Z. The native Escape route restores normal Edit, keeps
  the transformed selection, and does not open the pause menu.
- The transform screenshot was inspected for the left status panel, active-axis
  ring at the selection pivot, and replacement bottom hints. Modifier defaults were
  checked against the installed reference: Tiny = Left Ctrl, Small = Left Alt,
  Large = Left Shift. Configuration registration and the generated settings file
  were confirmed in the packaged game.

The runtime test blocks every save request and never loads an existing save. Its
hooks activate only with `-BuildingSkillsRuntimeTest`; normal play is unaffected.
Logs and screenshots are `Saved/RuntimeTest.log`, `Saved/RuntimeTest.png`, and
`Saved/TransformTest.png`,
relative to this repository. Reproduce with `scripts/RuntimeTest.ps1 -Render` while
Satisfactory is closed. Use `scripts/VerifyInstall.ps1` to recheck installed files.

The original startup-only checks missed two gameplay failures: the input guard
rejected the Build Gun's own interaction widget, and the requested Edit mapping
context was not activated. The guard now respects the widget's native blocking flags;
runtime context copies are registered directly with the enhanced-input subsystem.
The fixed keyboard shortcut list is extended with an SML widget hook rather than
relying on enhanced-input hint metadata.

Editor automation additionally covers nested groups, group merges retaining children,
one-level ungrouping and invalid selections, hierarchy normalization, save-property
serialization, modifier remapping, rebindable commands, and the scoped M/Map conflict.
Editor FactoryGame methods are stubs, so these tests cannot replace packaged-game checks.

The reported group-hover crash exhausted Unreal's UObject limit in the native
refund UI update called from `AddPendingDismantleActor`. Selection batches now
suppress those intermediate refund updates, Edit skips refunds entirely, and
unchanged groups retain their pending list. Spatial lookup no longer rebuilds a
whole building class each frame. The regression records UI-widget growth separately
from total UObject slots, because unrelated world streaming can allocate objects
while the player remains stationary.

## Remaining manual coverage

Transformed-building save/reload, collision and snapping across all supported building
types, multiplayer/late join, every scale-axis visual, and transformed blueprint
membership need manual coverage. Multiple placed blueprint expansion, complex nested
selections, live rebinding, and uninstall compatibility also need the matrix below. The runtime fixture uses storage containers, lightweight foundations
and default keys; it does not claim those broader cases or benchmark an existing
large factory. This build has not been packaged for Epic, Windows Server
or Linux Server.

Run `scripts/Mod.ps1 BuildEditor`, `scripts/Mod.ps1 EditAssets`, and
`scripts/Mod.ps1 Test` for editor checks. Reports are in `Saved/Automation`.

## In-game test matrix

Use a separate test save containing machines, foundations and two placed blueprints.

1. Confirm the bottom-right reminder includes the configured Edit shortcut (Alt+E
   by default), including after rebinding it. Open Edit from both holstered and
   equipped states; the left panel must say EDIT and the bottom hints must offer
   Group/Merge. Hover a large existing group in Edit and vanilla dismantle, then move
   between its machines and foundations; neither should freeze or crash.
   Hold the configured multi-dismantle key (default Ctrl) and
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
   First confirm that simply aiming at one machine/foundation highlights it and
   holding dismantle completes. Then repeat with a persistent multi-selection while
   moving the cursor: its hold timer must still complete.
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


## Transform manual checks

1. Select separate buildings, a blueprint, and nested groups in turn. Press T;
   the selection must stay fixed while looking around and holding the selection key.
2. Cycle all ten modes with the configured mode key. The colored rings/arrows and
   bottom hint must follow the active axes. Scroll both directions and verify the
   intended shared pivot and size changes. Check collision and subsequent snapping.
3. Turn to face each cardinal direction and try all arrow keys, then Page Up/Down.
   Rebind T, movement, modifiers and the normal Build Gun mode key; check the hints.
   Change each tier's movement, rotation, multiplier and repeat delay in mod settings.
4. Press Esc once: stay in Edit, retain the selection and changes, and restore Group,
   Merge, Ungroup and selection-mode controls. Re-enter Transform and repeat.
5. Try a selection containing a connected machine or spline building. The entire
   transform should be rejected with an explanation, leaving all buildings unchanged.
6. Save and reload a separate test world after mixed transformations; verify native
   transforms, inventories, group membership and blueprint selection. Then repeat
   with a host and client, late joining, and concurrent edits.
