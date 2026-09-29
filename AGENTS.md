# Building Skills workflow

- The mod reference is `BuildingSkills`; its display name is `Building Skills`.
- The plugin is in `mod/BuildingSkills`. Keep code, config and Unreal content here.
- Use the shared SML checkout at `F:/dev/satisfactory_mods/SatisfactoryModLoader`
  and CSS engine at `C:/Program Files/Unreal Engine - CSS`.
- Follow `docs/SETUP.md`. Do not modify another mod or upgrade the shared toolchain
  as part of this project's routine work.
- Commit source and content assets; keep generated binaries, caches and packages ignored.
- Never mention AI authorship, assistance or participation in commit messages or
  add AI co-author trailers. Document Codex participation in the README instead.
- Keep validation claims specific: compiling does not prove packaged gameplay.
- Edit opens on rebindable Alt+E. Group, Merge and Ungroup default to G/M/U.
  Normal/Blueprint/Group selection is shared with vanilla dismantle, using its existing
  configured multi-select and mode-switch controls. See docs/GROUPING.md.
  Transform (T) moves, rotates and scales supported buildings; Esc returns to Edit.
  Movement follows the nearest horizontal axis to the camera facing.
  See docs/TRANSFORM.md for controls and supported building types. Skill progression
  remains outside the current scope.
- Keep machine-specific absolute paths out of TESTING.md and references to other mods
  out of the README.
