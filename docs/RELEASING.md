# Release versioning

The current version lives in `VERSION`; tags use `v<version>`. v0.5.6 and v0.5.7 are
regular GitHub releases without an experimental suffix. Version numbers do not imply
complete compatibility or guaranteed frame rates.

## Publish a release

1. Preserve the previous executable, matching symbols and package.
2. Update `VERSION`, English release notes and the changelog. Export Canary
   modifications with `python tools/prepare_canary.py --export`.
3. Build with `tools/build-canary-game.sh`, with experimental diagnostics OFF and
   no version override. Check the compiled version marker and native image.
4. Run relevant host regressions. Distinguish them from console gameplay results.
5. Package corresponding AutoLog source with `tools/package-native-autolog.py`,
   then run `tools/package-release.py`. It creates `dist/PPSA50011.zip`, containing
   only the title folder, documentation and license/source notices, without games.
6. Preserve matching `eboot.bin`, `eboot.elf` and `llvm-pie.elf` under
   `build/symbols/<version>`. Review staged files for private data before pushing.
7. Publish a release from the matching source commit with two user downloads:
   `PPSA50011.zip` and the separately versioned AutoLog ELF. Keep the collector's
   complete corresponding source archive in `PPSA50011/licenses/`.
8. Verify the published assets, hashes and release status. Update the Desktop copy.

Publishing requires explicit user authorization. Do not post Discord messages,
reply to issues or close reports merely because a release was published.
