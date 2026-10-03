# Release versioning

The current version lives in the root `VERSION` file. Release tags are `v<version>`
and installable archives are named `PS5X360-v<version>.zip`.

- `0.1.0-preview`: first public preview, still experimental.
- Patch increments, such as `0.1.1-preview`: fixes and small refinements.
- Minor increments, such as `0.2.0-preview`: substantial features or compatibility changes.
- `1.0.0`: reserved for a future release that is deliberately declared stable.

Preview versions are marked as GitHub prereleases. Version numbers do not imply
that every Xbox 360 game is compatible.

## Publish the next version

1. Update `VERSION` and add English notes to `docs/releases/v<version>.md`.
2. Rebuild the native application and record what was actually verified on host and PS5.
3. Run `python tools/package-release.py`. It reads `VERSION`, adds release metadata
   to the ZIP and writes the matching `.sha256` file.
4. Commit the corresponding source and documentation, then push `main`.
5. Create an annotated `v<version>` tag on that commit and push the tag.
6. Create a GitHub release for that tag, attach its versioned ZIP and checksum,
   and use that version's notes. Keep preview releases marked as prereleases.
7. Download the uploaded files and compare their hashes with the local artifacts.

Keep older tags and published assets intact so users can return to earlier builds.
Changes after a release belong in a new version; do not silently replace its executable.

The original `preview-2026.10.03` tag is retained as a historical source marker.
Its original release remains available as a legacy preview. Versioned releases
start with `v0.1.0-preview`.
