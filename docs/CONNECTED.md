# Connected Components

Connected Components is a shared search used by Edit/Dismantle selection and
Customizer material/swatch application. R uses the existing Build Gun mode binding.
Select **Connected**, then press **K** (rebindable) for the common settings window.
Transform's temporary step window also defaults to K, in its separate state.

The search starts at the pointed building. World-axis bounding boxes count as
touching at faces, edges or corners. Each visited box expands on all sides by the
gap in meters, plus 0.1 cm for placement rounding. Only that reference box expands;
the gap is not doubled. Rotation and scale contribute to the world bounding box.
There is no layer filter, so touching floors above/below can be reached too.

The settings apply to all three entry points for the current player session:

| Setting | Meaning |
| --- | --- |
| Gap | 0–1000 meters; default 0 |
| Ignore swatch | Default off; match the seed's swatch, including custom colors |
| Ignore material | Default off; match the seed's material family |
| All | Any building type |
| Category | Same native building category as the seed |
| Subcategory | Same category and at least one shared native subcategory |
| Strict | Same building type (default); material variants may match when material is ignored |
| Object limit | 1–2048, including the seed; default 64 |
| Whitelist regex | Match building display names; empty allows any name |
| Blacklist regex | Exclude matching display names; overrides the whitelist |

Regex matching is case-sensitive unless an inline flag such as `(?i)` is used.
For example, `(?i)foundation` allows names containing foundation, and `4m$`
excludes names ending in 4m. Names follow the game's localization (the server's
localization for authoritative paint searches). Invalid expressions show an error
and cannot be applied. Matching has bounded time and stack use.

To include belts or pipes, use `(?i)(?:belt|pipe)` without quotation marks.
The `|` means either alternative; square brackets such as `[BeltPipe]` mean
any one of those characters, not either word. Matching searches within the
displayed name, so surrounding `.*` is unnecessary. Buildings without a recipe
descriptor still use their readable name, never an internal `Build_...` name.
Previously selected objects remain selected when filters change.

Filters always compare against the initial seed. Excluded buildings cannot bridge
the search to other buildings. The count limit stops expansion and reports that
the preview is capped. Existing Ctrl selection remains persistent across modes;
Connected adds/removes its current set through the same selection mechanism.

Customizer's **Toggle spread** control switches between **Default** and
**Connected**, with the current spread shown below the cursor. Connected previews
the pending material or swatch on the affected buildings before applying it.
The preview is temporary. Changing targets retains previews of shared members,
removes departed members and adds newly reached ones; leaving the mode clears it.
A Connected click resolves
membership on the server before changing any material or swatch. Individual
applications use native customization execution, costs/refunds and replication.
Ineligible buildings are skipped, and insufficient materials stop application.
Patterns and skins keep native behavior. Connected changes existing native
building customization, not a new saved component graph.

The settings window uses Apply/Cancel; Escape cancels the draft. It releases game
mouse capture, blocks camera input and restores it on close. Settings are shared
locally for the player session and are not written into world saves.
Text fields accept AltGr combinations such as AltGr+7 for `|` without forwarding
unhandled key presses to game shortcuts.

Tap the configured mode key (R by default) to cycle; hold it for the native radial
menu. Edit/Dismantle include their selection modes, Transform exposes its ten
rotation/scale choices, and material/swatch painting offers Default and Connected.

Search uses the game's actor octree and lightweight spatial grid to visit nearby
buildings instead of scanning the entire factory on each hover. Each candidate
still passes the same precise bounds and filter checks. Moves update these native
indexes. Periodic refreshes pick up nearby construction, removal and customization
changes; unchanged preview actors are reused.
