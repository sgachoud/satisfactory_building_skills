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

The native lightweight reference includes a runtime slot and cached transform but
has no documented durable save identity. Building Skills therefore saves the class,
recipe and full transform, not the slot. After load it resolves only a unique match.
Per-frame spatial buckets avoid scanning all instances for every selected member.
Deletion hooks remove memberships before slots can be reused. Exact overlapping
instances with identical fingerprints fail closed. Moving buildings in a different
mod can invalidate fingerprints; transform tracking is outside this iteration.

No lookup runs during PostLoadGame: game actors and lightweight data may still be
loading. Unresolved/deleted members are excluded from selection; PreSave prunes
unresolved memberships and empty groups. Original buildings are never removed by
metadata cleanup.

## Input and dismantle integration

The original Alt+E mapping is a child of the normal player-actions context.
A separate explicitly bound Edit parent activates a discoverable child context for
Group, Merge and Ungroup. The child carries player-mappable action settings.
The vanilla dismantle state continues binding its own multi-select action and the
Build Gun continues handling mode switching, including rebinding and its mode UI.

Edit uses the native dismantle trace and highlighting machinery. Native hooks keep
its trace, timing, disqualification and refunds, while replacing per-mode selection
expansion with the shared persistent list. Pending actors are fed back into the game
and lightweight temporary actors are pinned only while selected. Edit blocks both
primary dismantle execution and the server dismantle request. Ordinary dismantle
continues through the native pipeline.

Hooks are installed in game builds only: FactoryGame implementations in the editor
checkout are stubs. SML Friend access transformers expose the exact integration
points without editing shared game headers. These hooks require packaged-game
validation whenever Satisfactory/SML changes.

## Authority and scope

A registered player-owned remote call object sends Group/Merge/Ungroup requests.
The server checks Edit state, character availability, selection size and validity,
range to the selection, and a hierarchy revision to reject stale concurrent requests.
The client applies returned selection only after the corresponding replicated
revision arrives. The subsystem is registered by the root game-world module;
the remote call object is registered by the root game-instance module.

Automated hierarchy/save-property tests do not prove the game's own save traversal,
lightweight reconstruction, replication, or uninstall behavior. Those require the
manual matrix in TESTING.md.
