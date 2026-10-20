# Building Skills

Tools for editing your factory and planning what comes next.

Group buildings, move whole selections, copy layouts, repeat construction with
mirrors and rotations, and design rail networks before building them.

**Early-access release: 0.10.9.** Back up an important save before large editing
operations. Some recent planner changes still need broader player testing.

## Start here

Install Building Skills through Satisfactory Mod Manager.

- **Alt+E** opens the Edit Gun.
- **Alt+P** opens the Plan Gun.
- **K** opens the settings for your current Building Skills tool.
- Change persistent defaults under **Mods → Building Skills**.
- Rebind commands in the game's keybinding settings.

In multiplayer, install the **same version on the server and every client**.
No additional content mods are required.

## Select and organize

Edit and Dismantle share five selection modes:

- **Normal:** individual buildings.
- **Blueprint:** placed blueprints.
- **Group:** persistent groups, including nested groups.
- **Connected:** touching buildings, filtered by type, category, material,
  swatch and optional name patterns.
- **Volume:** building centers inside an adjustable sphere or box.

Use the game's multi-select binding (normally **Ctrl**) and aim at objects to
add or remove them. **R** switches selection mode; hold it for the mode selector.
Selections persist across mode changes.

In Edit, **G** creates a group, **M** merges, and **U** ungroups the selected
outer group. Empty groups are removed automatically.

## Move, rotate and scale

Press **T** in Edit to transform your selection. Arrow keys move it horizontally;
Page Up/Down move vertically. Use **R** and the scroll wheel for rotation and
scaling. Choose Local, World or Anchor axes, a shared pivot, and configurable
small/normal/large steps. **K** lets you type temporary step values.

Connected belts, pipes and rails adjust with supported edits. Power connections
follow their endpoints. **Esc** returns to normal Edit.

## Copy and blueprints

- **Alt+C:** copy the current selection into a placement hologram.
- **Alt+S:** save the selection through the blueprint details window.
- **Alt+A:** set or clear a building as the alignment anchor.
- **Alt+D**, with a blueprint or copy hologram: cycle duplicate handling through
  Off, Replace and Skip.

Selections do not have to be grouped. Copies preserve nested group structure.
Without an anchor, saved selections align horizontally with their dominant
orientation and use a bottom-centered origin.

Building scales are preserved in blueprint files. **Building Skills is required
to preserve those scales during placement**; vanilla can read the blueprint but
places its buildings at normal scale.

## Repeat with transformers

In Edit, choose an anchor with **Alt+A**, then add a transformer with **Alt+X**.
Configure the list under **K**.

Mirror planes and rotation axes repeat construction within an effect radius,
with hologram previews. Ordered transformers can repeat the player's placement
and the output of earlier transformers. Matching buildings can also receive
repeated deletion, material/swatch changes, movement, rotation and scaling.

Transformers belong to each player and last for the session. Copies use normal
building costs. Mirroring preserves the game's building models and prioritizes
input/output orientation; it cannot produce a perfectly reflected version of
every asymmetric machine.

## Stick placement

While placing a spline building, **Alt+S** cycles Stick through Off, Default and
Constrained. Place endpoints at a chosen distance from your view, starting at
64 m. Scroll changes distance; **Alt+scroll** tilts vertically and **Ctrl+scroll**
tilts horizontally.

Configure slope, horizontal radius, constraints and steps with **K**. Long
routes are divided into connected segments while retaining native placement
validation. Nearby endpoints can snap using the game's configured snapping
modifier behavior.

## Plan rail networks

**Alt+P** opens the rail planner. Place connected nodes, edit their positions and
directions, split curves, and add parallel tracks. Curve, Straight, Free
through-point and Free Bézier modes cover different editing needs.

Plans are visible only while the Plan Gun is open. Personal plans belong to
their player; shared plans can be edited and built by everyone. Plans are saved
with the world. The K menu supports automatic loading on selection, duplication,
renaming, parallel offsets, segment lengths and automatic block signals.

Use **Build full plan** to validate the routes and construct rails with normal
inventory/Dimensional Depot costs. Planning itself consumes no materials.

## Painting and undo

In the Customizer, **R** switches spread between Default and Connected. **K**
configures matching and the object limit; the default is 64.

**Ctrl+Z / Ctrl+Y** undo and redo construction in Build, deletion in Dismantle,
and plan edits in Plan. Transform has its own **Z / Y** history. Histories count
actions rather than individual objects and last for the session.

## Compatibility and limitations

- Requires **SML 3.12**; the package declares game build **502094 or newer**.
  That minimum is not a promise of compatibility with future game updates.
- Windows client and dedicated-server target availability is shown on the release.
- A selection is limited to **2,048 buildings**. Larger operations are rejected.
- Exact overlapping lightweight buildings may be ambiguous for grouping or undo.
- Groups and rail plans are mod save data. Removing the mod is not a verified
  lossless migration; keep a backup if you intend to uninstall it.
- This is a building toolkit. Skill progression is not implemented.

Report bugs through the [issue tracker](https://github.com/sgachoud/satisfactory_building_skills/issues).
Include the mod/game versions, solo or multiplayer, the action
and building types involved, reproduction steps, and the relevant client/server
logs. Remove private information from logs before sharing them.

## License

Building Skills is published under the MIT license. Dependency notices are
included in the download.

[Source code and detailed documentation](https://github.com/sgachoud/satisfactory_building_skills)
