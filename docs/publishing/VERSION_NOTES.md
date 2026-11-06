Fixes rail-copy connections, rail movement and paint, Plan endpoint snapping, and saved-blueprint inventories.

- Removes source-world rail links before loading copies, addressing delayed connection-update crashes with rotation transformers. Separately placed copies reconnect to their correct transformed neighbours.
- Preserves rail curves and painted swatches during movement and undo. Invalid distant connections are rejected before they can deform a transform.
- Finds rail endpoints along the aiming ray, including when the cursor misses the rail mesh. Ctrl still disables snapping.
- Includes the native bend-radius margin in pipe and hypertube previews.
- Saved selection blueprints omit stored goods and their construction cost while retaining installed Power Shards and Somersloops. Original inventories and deletion-undo contents remain intact.

Requires Satisfactory 1.2.5.0 (CL 517554) or newer. Update Building Skills on both clients and servers.

Existing damaged rail links are not repaired automatically.

Validation: 431 Plan assertions passed during development, followed by 499 assertions covering transformers, network movement, undo and blueprints in the isolated Steam game. The latest changes have not been rerun in dedicated multiplayer; Epic, Windows Server and Linux Server are compiled and packaged without gameplay validation for this version. Full results and limitations: https://github.com/sgachoud/satisfactory_building_skills/blob/main/docs/TESTING.md
