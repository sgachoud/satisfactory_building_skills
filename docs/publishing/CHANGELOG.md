# 0.10.9 — Initial public release candidate

Building Skills brings together:

- Edit Gun with persistent nested groups and shared Normal, Blueprint, Group,
  Connected and Volume selection.
- Movement, rotation, scaling, configurable frames and pivots, and transform history.
- Saving selections as blueprints, clipboard placement, alignment anchors,
  blueprint scale preservation and duplicate handling.
- Ordered mirror/rotation transformers with live previews and matching edit,
  paint and dismantle actions.
- Connected painting and construction/dismantle undo and redo.
- Stick endpoint placement with configurable distance, tilt, slope, radius and
  subdivision for long routes.
- Plan Gun with saved personal/shared rail plans, editable curves, linked
  parallel nodes, rail offsets, segment-length policies and block signals.

Recent fixes and refinements:

- Saved defaults now populate Stick, Volume and Transform configuration through
  SML's reflection bridge, including Connected, history and planner defaults.
- Existing rail-plan geometry keeps its orientation when extending a terminal.
- Added shape-preserving reversal of an individual plan spline.
- Added signed parallel node offsets and rail start offsets.
- Saved plans load on selection, support double-click renaming, and can be duplicated.

This release is early access. Windows and Linux builds and focused configuration
automation have been checked; these do not establish that every feature works in
every game, mod combination or multiplayer setup. See the accompanying release
verification report for exactly which targets and checks were completed.
