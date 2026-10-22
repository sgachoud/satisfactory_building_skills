# Publication kit

Building Skills is published on the Satisfactory Mod Repository (SMR,
ficsit.app); Satisfactory Mod Manager (SMM) distributes versions from there.

## Files

The generated `Saved/Publication/0.10.14` folder contains the upload archive,
listing, metadata, changelog, artwork, source snapshot and verification report.
Only **BuildingSkills.zip** is the mod version upload. Do not upload a
development platform ZIP, source snapshot, or the complete publication folder
as the mod version.

- `OVERVIEW.md`: concise description with six GIF previews and documentation
  links, ready to paste. `LISTING.md` retains the earlier detailed draft.
- `PREVIEW.html`: local gallery that plays the GIFs without requiring an upload.
- `METADATA.json`: name, reference, short description and disclosure field text.
  This is a preparation record, not an SMR API payload.
- `CHANGELOG.md`: version description, ready to paste.
- `media/`: six optimized GIF previews made from the publisher's recordings,
  with conversion settings and source hashes in `manifest.json`.
- `VIDEO_SHOT_LIST.md`: the original recording suggestions, retained for future
  demos. Six gameplay clips have now been supplied and converted.
- `icon-512.png`: mod icon. `banner-1600x900.png`: original promotional artwork,
  not a gameplay screenshot.
- `verification.json`: exact archive validation and target/build evidence.
- `SHA256SUMS.txt`: hashes for the prepared deliverables.
- `BuildingSkills-0.10.14-source.zip`: source, assets, scripts, documentation and
  notices, excluding local saves, references, logs, caches, SDK and binaries.

## Create the page and version

1. Sign in at https://ficsit.app using the publishing account.
2. Check whether this account already owns a Building Skills page; update it if
   present rather than creating a duplicate. Otherwise choose New Mod.
3. Set the name to **Building Skills** and the permanent mod reference to
   **BuildingSkills**. Paste the short/full descriptions and add the icon.
4. Complete **Network Activity Transparency** and **AI Usage Transparency** using
   the prepared text. Keep these separate from the feature description.
5. Use the source URL in `METADATA.json`. The support URL is already included
   as the overview's “Report a bug” link; SMR has no separate support field.
   The public GitHub repository has issues enabled and the initial release
   source is published. Push the reviewed update source before its release.
   Include `docs/publishing/media/` on branch `main`: the overview embeds these
   GIFs through raw GitHub URLs. Until the source is pushed, those public image
   links will not resolve. Alternatively upload the GIFs to SMR's media service
   and replace the image URLs before submission.
   Suggested tags must be selected from the site's actual list.
6. Create New Version, select **BuildingSkills.zip**, verify version **0.10.14**,
   target platforms and the SML/game requirements, and paste the changelog.
7. Choose early-access/beta wording rather than claiming a stable release.
   Do not mark an untested game branch as working just because it compiled.
8. Review the page, disclosures and release report, then submit when ready.

No page, remote repository, release or public upload is created by preparing
this kit. The login and final publication happen on the publisher's account.
The supplied SSH remote is configured as `origin`, with `main` as the branch.

## Recreating the package

The descriptor's integer `Version` must equal the major component of
`SemVersion`: for `0.10.9`, use `Version: 0`. It is not a build counter.
`VersionName` must match `SemVersion`. The publication validator checks this
in the source descriptor and every packaged target.

Follow `docs/SETUP.md` for the pinned SDK. All platform variants must be built:
Steam and Epic clients, Windows Server and Linux Server. `Mod.ps1 Release`
builds those native targets through the isolated helpers, then Alpakit cooks
and combines the platform folders.

The resources directory carries the project's MIT notice and dependency notices.
Regenerate artwork with `scripts/create_publication_art.py` (Python + Pillow).
Regenerate gameplay previews with `scripts/create_feature_gifs.py --ffmpeg
<path-to-working-ffmpeg>` (Python + Pillow), using the originals in
`Saved/Publication/0.10.9/videos`. The local fallback encoder lives under the
ignored `Saved/PublicationTools`; it is not included in the mod or source ZIP.
GIF previews are page media, never runtime mod content. The original recordings
remain unchanged; longer menu sequences are trimmed and three clips are sped up.
Run `scripts/PreparePublication.py` with the merged Alpakit archive to validate
and assemble the upload kit. The script checks descriptors, required native
targets, cooked content, licenses, and unsafe archive entries. It removes debug
symbols from the upload while retaining the original local Alpakit archive.

Keep the final report honest: archive checks are not multiplayer gameplay tests.
The 0.10.9 configuration regression passed before publication. All 28 Editor
automation tests passed for 0.10.13, including synchronized parallel geometry,
offsets and length limits. Packaged construction, menu actions, Plan entry,
native owner-identity serialization and live multiplayer reload still need
confirmation after updating both client and server through SMM. The SDK editor
cannot test the native identity APIs because their editor implementations are stubs.

## References

- [Official upload guide](https://docs.ficsit.app/satisfactory-modding/latest/UploadToSMR.html)
- [Release targets and descriptor fields](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/ReleaseMod.html)
- [SMR content and disclosure policy](https://ficsit.app/content-policy)
