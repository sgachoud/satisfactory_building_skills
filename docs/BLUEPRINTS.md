# Saving groups as blueprints

In normal Edit, select exactly one group and press **S / Save**. The action is
rebindable under Building Skills. It appears beside Ungroup in the native bottom
hint bar. It is unavailable for individuals, multiple groups, mixed selections,
and while transforming. Its input context is removed while unavailable, allowing
the usual S movement binding to work.

The native blueprint window provides name, description, icon, color, category and
overwrite confirmation. **Load and Clear are omitted**. Closing the window returns
to Edit with the group selected. Saving includes all nested group members once;
it creates a normal blueprint without removing or moving the original buildings.
The blueprint origin is centered horizontally at the bottom of the group's bounds.
Its dimensions expand to contain the selected content.

An owner-only, transient save session supplies the native designer interface with
the selected members and cost. It creates no platform, storage or construction
volume and is never saved with the world. The normal game writer handles blueprint
files and descriptors. Lightweight members are resolved to their native temporary
actors immediately before serialization. Server validation checks ownership,
group revision, building identity, group size and player range again on Save.
If the group changes while the window is open, close it and select the group again.

Blueprint bounds include the lightweight subsystem's local mesh bounds transformed
into world space. Temporary foundation actors have no mesh components, so measuring
only actor components would reject foundation-only groups and undersize mixed groups.

Load and Clear are also blocked in the native save-session methods so controller
shortcuts cannot place or dismantle buildings through this window. Ordinary
blueprint designers retain their normal functionality.

For online testing, install the same Building Skills version on the Windows client
and Linux server. Check opening, saving, overwriting, closing without saving,
simultaneous players, a changed group while the window is open, and blueprint file
availability on clients. Compiling a Linux package does not verify multiplayer.
