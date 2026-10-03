# Development build notes

The downloadable release is a native PS5 title. The root project's older probes and the active Canary application are different targets.

## Active application

- Core: Xenia Canary, pinned in `deps.json` to `b083312b8b18e07e6e410b82104191f126722794`.
- PS5 core patch: `patches/canary/xbox360ps5.patch`.
- Application entry point: `src/canary_game_main.cpp`.
- Launcher and translations: `src/launcher.cpp`, `include/xbox360ps5/i18n.hpp`.
- Native platform adapters: `platform/ps5/`.
- Native build definition: `canary/CMakeLists.txt`.
- Executable build/link script: `tools/build-canary-game.sh`.

## Source dependencies and local layout

The current build scripts still depend on a prepared local toolchain. This is **not yet a self-contained, one-command build from a fresh clone**.

The development layout provides:

1. A PS5 payload SDK, selected with `PS5_PAYLOAD_SDK` for CMake.
2. The prepared [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) tree at `.deps/references/PS5_Vulkan`, including its SDK wrappers, static RADV build, zlib, linker recipe and native module builder.
3. A staged native runtime under `build/native-runtime-stage`, providing the image verifier and application runtime module.
4. Clang/LLVM 18, CMake, Ninja, Python 3 and the upstream build dependencies.

The linker script uses the SDK and RADV assets inside the reference tree in addition to the CMake SDK variable. Merely setting `PS5_PAYLOAD_SDK` is insufficient if those reference assets are missing.

For source origins and revision information, see [Credits](CREDITS.md). Dependencies and games are intentionally excluded from the Git repository.

## Prepare and build

In the prepared environment:

```bash
# Fetch the pinned core and apply the PS5 patch.
python3 tools/prepare_canary.py

# Build the current native application.
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk bash tools/build-canary-game.sh
```

Outputs:

```text
build/canary-game/eboot.elf
build/canary-game/eboot.bin
```

`tools/build-canary-game.sh` validates mapped segments and relocation targets before producing the final executable. This validation does not demonstrate gameplay compatibility.

## Local language regression

```bash
clang++ -std=c++20 -Iinclude tools/check-ui-language.cpp -o check-ui-language
./check-ui-language
```

This checks system-language regional variants, English fallback and interface translations. Native PS5 system-service calls still require hardware verification.

## Package a release

The staging folder `dist/PPSA50011` must contain the runtime, title metadata, artwork, fonts, sounds and community patch files. The packager takes the executable from the **current Canary build**, rather than trusting a previously staged executable.

```bash
python3 tools/package-release.py
```

Output: `dist/PS5X360-release.zip`, including the English installation guide, release notes, license notices and a per-file SHA-256 manifest. Game files are not packaged.

## Historical documentation

Earlier component probes and their build targets are documented in [the port history](PORT_HISTORY_PT.md). Those notes are retained in Portuguese and may describe superseded stages. Use this document and the release notes for the current application.
