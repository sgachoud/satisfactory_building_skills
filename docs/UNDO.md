# Build and Dismantle undo

Ctrl+Z and Ctrl+Y are the shared rebindable **Undo / Redo** actions in the
Building Skills keybindings. Their native bottom hints change with the active gun:
Build undoes or reapplies construction; Dismantle restores or reapplies deletion.
Edit uses its transform history and Plan uses its geometry history. The actions
are inactive while a settings window has input focus.

The mod configuration contains **Undo history (actions per gun)**, default 32,
with 0 disabling capture and a maximum of 256. In multiplayer the server setting
controls retention. Histories belong to individual players and are session-only;
they are not saved to game saves. A whole native placement/deletion transaction is
one action, regardless of how many buildings it contains.
Each gun has its own redo stack. A new action clears that gun's redo history.
Redo uses the same connection checks, native refunds and repayment rules as undo.

Restored buildings bypass construction animations so deletion undo does not wait
for a blueprint build effect before restoring visibility and collision.

Deletion undo uses an in-memory native building archive, supplemental inventory and
conveyor/fluid contents, connection references, blueprint identity and group metadata. It takes the refunded
items back from the player's inventory or the original overflow containers. If
refunds have been spent or transferred, return them to your inventory before undo.
It rejects restoration when an outside connection is missing, moved or occupied,
or a power port lacks capacity. Rail signals retain their guarded/observed orientation.

Construction undo uses native dismantling and refunds. Upgrade replacements are
retained as a history barrier: dismantling the upgraded object alone would lose
the original building. It rejects buildings moved
or removed after construction, protected buildings, and dependencies added outside
that construction action. Move within gun range of the affected buildings to undo.
A partial or uncapturable deletion remains a history barrier with an explanatory
message, rather than unexpectedly undoing an earlier action.

Archive serialization preserves native dependency objects, including dependencies
owned by a building outside the selection. These are complete archive entries,
not individual connection references. Invalid entries reject the snapshot safely;
copy connection isolation remains applied to loaded objects before native cleanup.
Already-dismantled buildings are excluded from capture during their removal effect.

Validation is tracked in [TESTING.md](TESTING.md). Compilation alone does not
establish packaged gameplay behavior.
