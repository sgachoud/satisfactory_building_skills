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
