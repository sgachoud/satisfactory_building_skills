Updates Building Skills for Satisfactory 1.2.5.1 (CL 520140) and SML 3.12.1.

- Rebuilt with refreshed CL 520140 headers, CSS engine build 87 and matching Wwise integration.
- Uses native Save-window interface dispatch and conveyor fields, replacing the old-header callback and offset workarounds.
- Requires SML 3.12.1, which fixes multiplayer joining on CL 520140.
- Includes the 0.11.3 fixes for Copy/Save snapping bounds, stored-goods exclusion with installed upgrades preserved, conveyor/lift handling and false first-move rejections.
- Corrects the game-version label to 1.2.5.1; the minimum changelist remains 520140.

Update Building Skills and SML on both clients and servers.

Validation: 28 editor tests and 1,700 rendered Steam assertions passed. A Steam client also passed native blueprint placement, deletion and deletion undo/redo on an isolated Windows dedicated server. Smart and ARS integrations were skipped. Epic and Linux gameplay were not tested. All four native targets compiled and passed package validation; see https://github.com/sgachoud/satisfactory_building_skills/blob/main/docs/TESTING.md for exact scope.
