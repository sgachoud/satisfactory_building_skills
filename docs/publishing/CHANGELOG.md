# 0.11.4 - SML 3.12.1 and refreshed game headers

- Updates the build environment to CSS engine 5.6.1-css-87 and SML 3.12.1, with game headers matching CL 520140.
- Uses native Save-window interface dispatch and conveyor fields again, replacing the old-header callback and relative-offset workarounds.
- Requires SML 3.12.1, which fixes joining multiplayer on CL 520140.
- Corrects the human-readable game version to Satisfactory 1.2.5.1; the required changelist remains 520140.

Requires Satisfactory 1.2.5.1 (CL 520140) or newer. Update SML and Building Skills on both clients and servers. Validation is recorded in docs/TESTING.md.

# 0.11.3 - Copy, blueprint bounds and game compatibility

- Fixes Save-window interface dispatch on Satisfactory 1.2.5.1 (CL 520140), which could corrupt the player during native interaction callbacks. The correction is limited to Building Skills' temporary Save context.
- Resolves factory inventory and conveyor fields through the running game instead of obsolete SDK offsets. This covers preserved machine upgrades, conveyor/lift movement and deletion-undo item restoration.
- Resolves spline interfaces through the running game for Copy and saved-blueprint preview costs, including conveyor segment length.
- Fixes blueprint snapping gaps caused by vehicle-path visualization and collision bounds (issue #4). Copy and selection Save now use native snapping clearance, including exclusions, rotation and scale; ordinary blueprint placement no longer expands its native snap box to render meshes.
- Removes destroyed actors' edit-version entries so newly allocated rails, signals or other buildings cannot inherit a deleted object's version and fail their first multiplayer move. Real stale-edit checks remain enabled.
- Copy now omits stored and production goods from the clipboard and construction cost, matching selection blueprint saving. Installed Power Shards and Somersloops remain. Transformer construction uses the same copy path; deletion undo still retains contents.

Requires Satisfactory 1.2.5.1 (CL 520140) or newer. Update both client and server. Validation: 419 passing isolated Steam assertions; dedicated multiplayer was not rerun for this version.

# 0.11.2 - Rail connections and blueprint inventories

- Saving a selection as a blueprint omits storage and production inventories from both the archive and construction cost. Installed Power Shards and Somersloops remain. Source inventories and deletion-undo contents are preserved.
- Rail copies remove external source-world connections before native rail loading can repair them onto the original network. This targets the deferred connection-sorting crash reported while placing rails with rotation transformers.
- Rails placed in separate transformer actions reconnect to the matching transformed neighbour at the actual endpoint, without retaining links to the original network.
- Moving an unchanged rail curve preserves its spline and instance buffers. Necessary spline rebuilds reapply the saved painted swatch, including during undo. Invalid distant rail links are rejected before they can distort a transform.
- Plan nodes find existing transport endpoints along the aiming ray, including when the cursor misses the rail mesh or hits its surface just before the connector. Instanced collision owners are resolved too; Ctrl still disables snapping and Stick snapping stays near its tip.
- Pipe and hypertube previews include the native 5% minimum bend-radius margin, preventing borderline turns from looking valid until construction rejects them.

Requires Satisfactory 1.2.5.0 (CL 517554) or newer. Update both client and server. These changes prevent new bad rail links; they do not repair links already stored in a save. Validation and limits are recorded in docs/TESTING.md.

# 0.11.1 - Game update compatibility

- Fixes deletion undo crashing after game update 1.2.5.0 (CL 517554).
- Resolves native blueprint storage through runtime reflection, covering undo, copy, blueprint saving, scale restoration and preview cleanup.
- Restores Plan endpoint discovery when rail collision is represented by an instance rather than the track actor.
- Refreshes the gameplay regressions for the current selection modes and settings menus.

Requires Satisfactory 1.2.5.0 (CL 517554) or newer. Update both client and server. Validation is recorded in docs/TESTING.md.

# 0.11.0 - Mixed transport plans and blueprint designer selection

- Connected selection and material/swatch spreading now work on buildings inside blueprint designers.
- Adds Delete plan with confirmation in the saved-plans menu.
- Main and parallel lines can independently build rails, pipes, belts or hypertubes, including native pipe/belt tiers.
- Adds separate segment lengths for each type, native endpoint connections, and belt flow following direction arrows.
- Mixed synchronized parallels respect every selected type's length limits; block signals apply only to rail lines.
- Construction choices persist with plans and participate in duplication and editing undo/redo.

Update both client and server. Validation is recorded in docs/TESTING.md.

# 0.10.14 — Blueprint construction crash

- Fixes a server crash in blueprint construction caused by treating a construction-message interface pointer as a hologram actor.
- Corrects the same pointer handling in blueprint deduplication serialization and keeps its hook separate from Stick's serializer.
- Adds regressions for Copy, saved blueprints, Off/Replace/Skip message round trips and placement through a real multiplayer construction RPC.

Update both client and server. See the verification report for completed checks.

# 0.10.13 — Synchronized parallel rail joints

- Adds **Sync parallels in turns** under Plan's **K → Build specification**, off by default.
- Uses corresponding main-curve positions for parallel rail joints, including signed rail placement offsets.
- Reduces shared segment spacing when an outer rail would exceed the native maximum. Large backward extensions receive additional joints.
- Keeps live intersections as mandatory joints and checks minimum lengths before construction.
- Saves the checkbox with the plan and supports multiplayer, duplication and undo/redo.

Update both client and server. See the verification report for completed checks.

# 0.10.12 — Personal plans and Plan menu fixes

- Personal plans now retain a saved owner identity across PlayerState replacement on reload or reconnect. Existing owner references are migrated when available.
- Opening Plan requests a fresh snapshot; delivery is acknowledged only after the client accepts it.
- Plan menu actions keep the window open and refresh controls after the server confirms the edit.
- Retains the Plan activation fixes from 0.10.11.

Update both client and server. Existing plans can be recovered only if their data and an identifiable owner remain in the save. See the verification report for completed checks.
