Fixes deletion-undo crashes and restores Plan node snapping to rails after Satisfactory 1.2.5.0 (CL 517554).

- Corrects blueprint storage access after the game update, covering undo, copy, blueprint saving, scale restoration and preview cleanup.
- Finds nearby rail endpoints even when their collision belongs to the instance manager.

Update Building Skills on both clients and servers.

Validation: eleven isolated Steam gameplay suites passed, plus a Steam client/Windows dedicated-server test of blueprint placement, deletion, undo and redo. Epic and Linux builds are included but were not run on those platforms. Full results and limitations: https://github.com/sgachoud/satisfactory_building_skills/blob/main/docs/TESTING.md
