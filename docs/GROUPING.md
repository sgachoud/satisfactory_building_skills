# Grouping implementation

The supplied nested-groups design is treated as a feature specification. This
iteration implements metadata, selection, Group, Merge, Ungroup, and dismantling.
The document's transformation and scaling discussion describes future work.

## Hierarchy

`ABSGroupSubsystem` is an SML server-spawned replicated subsystem implementing
`IFGSaveInterface`. Its schema-versioned save data contains group IDs, names, parent
IDs, direct building references, and pivots. It never attaches or reparents game
buildings. Child groups are derived from parent IDs.

A building has at most one direct group owner. Normalization removes duplicate
selection and descendants covered by selected ancestors. Commands first construct
and validate their result; cycle checks prevent invalid hierarchy mutations.
Groups selected together become children under Group, while Merge preserves the
first selected group ID and moves other selected containers' contents into it.
Nested children survive Merge. A common immediate parent is retained; selections
across branches produce a root group. Ungroup promotes contents exactly one level.

Blueprint selection expands its actor-backed and lightweight members into individual
references. This changes neither the blueprint proxy nor the buildings' blueprint
membership. Selecting individual members already in custom groups extracts only
those members when grouping them.

## Lightweight identity

Connected selection is a fourth shared selection mode. It expands through the
reusable bounding-box/filter query described in [CONNECTED.md](CONNECTED.md), then
feeds individual references into the same persistent selection and native
dismantle pipeline. K opens the common settings without clearing pinned selections.

The native lightweight reference includes a runtime slot and cached transform but
has no documented durable save identity. Building Skills therefore saves the class,
recipe and full transform, not the slot. After load it resolves only a unique match.
Spatial buckets are cached by building class and invalidated by native addition,
replication and removal hooks (with array-size checks as a fallback). Hovering no
longer scans all instances of a class each frame. Matching still checks the actual
recipe/transform and rejects ambiguous coincident instances.
Deletion hooks remove memberships before slots can be reused. Exact overlapping
instances with identical fingerprints fail closed. Moving buildings in a different
mod can invalidate fingerprints; transform tracking is outside this iteration.

No lookup runs during PostLoadGame: game actors and lightweight data may still be
loading. Unresolved/deleted members are excluded from selection; PreSave prunes
unresolved memberships and empty groups. Original buildings are never removed by
metadata cleanup.

## Input and dismantle integration

The original Alt+E mapping is a child of the normal player-actions context.
An SML widget hook adds the configured shortcut to the actual bottom-right keyboard
shortcut grid. That grid does not consume enhanced-input button hints automatically.
Runtime copies of the input contexts are bound through the local player's enhanced
input subsystem. The Edit operation context is present only while Edit is open;
the discoverable child-context assets retain the player-mappable settings and names.
Owning the runtime copies prevents native context changes from removing the shortcut.
Ungroup remains conditional on a single selected group in the bottom hints.
Edit uses the vanilla equipment `Widget_HintContainer` and its `SetKeybindingHints`
function. Its parameter array is scoped and destroyed after dispatch, and it only
rebuilds when bindings or available commands change. The status panel sits on the
left; the shortcut hook adds a column inside the keyboard helper.
The vanilla dismantle state continues binding its own multi-select action and the
Build Gun continues handling mode switching, including rebinding and its mode UI.

Edit has a dedicated input component above equipment input. Its visible HUD replaces
the native dismantle overlay while editing and restores it on exit. The input guard
uses interaction widgets' native input-blocking flags: the gun HUD is itself an
interaction widget, so rejecting every open interaction widget would disable the gun.
Vanilla key labels resolve the keyboard context's mapping name, including remaps.

Edit uses the native dismantle trace and highlighting machinery. Native hooks keep
its trace, timing, disqualification and refunds, while replacing per-mode selection
expansion with the shared persistent list. Pending actors are fed back into the game
and lightweight temporary actors are pinned only while selected. Edit blocks both
primary dismantle execution and the server dismantle request. Ordinary dismantle
continues through the native pipeline.

Selection expansion and temporary-actor rebuilding happen only when selection or
group membership changes. Native refund calculation is suppressed during a batch
and runs once afterward for dismantle; Edit never requests dismantle refunds.
This prevents per-member UI construction and redundant work when hovering a group.
HUD counts use the resolved selection cache rather than expanding groups again.

Edit entry waits until the gun is equipped in the dismantle state before applying
its Edit descriptor and binding its commands. The native target setter is allowed
to complete, and selection reads its validated target afterward. Normal single-object
dismantling retains the native target. Expanded/persistent selections use the pending
list; native hover changes cannot reset its hold timer unless the selection changes.
Blueprint mode traces buildings and expands their membership, rather than targeting
the vanilla whole-blueprint proxy volume.

Hooks are installed in game builds only: FactoryGame implementations in the editor
checkout are stubs. SML Friend access transformers expose the exact integration
points without editing shared game headers. These hooks require packaged-game
validation whenever Satisfactory/SML changes.

## Authority and scope

A registered player-owned remote call object sends Group/Merge/Ungroup requests.
Selection uploads and operation results use acknowledged packets of at most 32
entries. This keeps a 900-object selection below Unreal's per-message limit,
including an Ungroup result that returns every member. The server executes only
after the entire selection arrives. Invalid sequences, changed metadata and
oversized totals are rejected; inactivity times out without discarding the local
selection or permanently blocking the controls. Progress extends the deadline.
Client and server need the 0.9.5 selection-transfer protocol for this path.
The server checks Edit state, character availability, selection size and validity,
range to the selection, and a hierarchy revision for selected groups to reject
stale concurrent requests. Explicit individual buildings are validated against
their current server identities without depending on unrelated hierarchy changes.
Autosaves only advance the hierarchy revision when they actually prune members
or groups.
The saved group tree is transferred in bounded, acknowledged chunks rather than
one replicated array. The client keeps its previous tree until a complete snapshot
passes validation, then applies the tree and its revision together. Returned
selections wait for that committed revision. Timed-out or malformed transfers are
retried without exposing a partial tree. The subsystem is registered by the root game-world module;
the remote call object is registered by the root game-instance module.

Automated hierarchy/save-property tests do not prove the game's own save traversal,
lightweight reconstruction, replication, or uninstall behavior. Those require the
manual matrix in TESTING.md.

## Empty groups

Deleting the last member removes its group immediately. Empty ancestors are pruned
recursively; a parent remains while any child still contains buildings. Both native
actor destruction and lightweight-instance deletion feed this cleanup. Save-time
pruning also removes unresolved members. Metadata cleanup never dismantles buildings.
