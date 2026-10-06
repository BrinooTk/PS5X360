# Changelog

## 0.5.6

- Improve shared memory lookups, invalidation granularity and CPU/GPU synchronization.
- Add adaptive dynamic vertex-buffer uploads with a Guide toggle.
- Improve game compatibility through shared memory, thread-lifetime and storage fixes.
- Add the Saved data panel, homebrew save storage and visible compiled build version.
- Include optional AutoLog 1.0.9 with better guest-crash classification and delivery diagnostics.
- Disable automatic intrusive CPU sampling in public builds; preserve manual measurement.
- See `docs/releases/v0.5.6.md` for installation and testing limits.

## 0.5.5-preview (local test build)

- Display the compiled version throughout the launcher and in the in-game Guide.
- Add explicit guest-kernel crash markers and guest context to per-game logs.
- Bound diagnostic address arithmetic at the guest's 4 GiB address limit.
- AutoLog 1.0.9-preview preserves the guest-kernel classification when register
  dumps are also present. These are diagnostics, not a universal crash fix.

## 0.5.4-preview (local test build)

- Fix final-owner thread-exit ordering on POSIX to prevent use-after-free of
  native thread mutexes after a guest closes its thread handle.
- Store saved content, profiles and achievements in the homebrew installation's
  `saves` folder, with an initial preserving copy and legacy fallback.
- Add Settings > Saved data in English, Portuguese and Spanish.
- AutoLog 1.0.8-preview classifies Guide returns as normal session completion.
- Host memory and migration regressions pass; console gameplay remains unverified.

## 0.5.3-preview

- Refined the game-library UI, cover-flow presentation, navigation and settings.
- Improved support for Xbox 360 ISO disc images, alongside extracted XEX games
  and GOD/STFS packages, with safer file reading and executable loading.
- Added compatibility fixes across multiple games, including loading, memory
  handling and version-matched patch selection. Compatibility still varies by title.
- Keep automatic guest video pacing at 60 Hz when emulated VSync is disabled,
  including the per-title patch override, to prevent accidental acceleration.
- Update VSync help in English, Portuguese and Spanish.
- Publish optional PS5-only AutoLog 1.0.5-preview with session-event reporting,
  a first-activation cutoff, durable retry queue and a reporting opt-out.

## 0.5.2-preview -- development build

- Open disc images and read-only packages without mapping them on the console: directory tables,
  headers and hash tables are read from the file as well as file data.
- Load executables from an unmapped disc image through the file reader.

## 0.5.1-preview -- development build

- Read disc images (ISO) and packages (STFS, GOD/SVOD) from the file with `pread` on the console
  instead of copying out of a whole-file mapping.
- Catch only `std::exception` at the kernel-call boundary (a catch-all took the forced unwinding
  that ends a thread on glibc hosts).
- Add `tools/make-test-iso.py`, which packs an extracted game folder into a disc image for tests.

## 0.5.0-preview -- development build

- Keep regenerable data (shader and module caches, guest `cache:` partitions) in the
  installation folder when it is writable; the save storage image keeps saves and profiles.
- Treat the instruction info cache as optional instead of aborting when its folder cannot be made.
- Return an unsuccessful status when a kernel call throws a C++ exception; report a launch that
  throws in the library after a clean restart.
- Validate the emulated console settings file and save it through a temporary file.
- Do not pin guest threads to host cores; hold whole-page write watches against neighbouring commits.
- Add a performance measurement to the guide and memory-watch counters to the periodic log line.


## 0.4.9-preview -- development build



- Detect ISO signatures without redundant full-disc mappings and directory walks.

- Close signature probes on every path and check file metadata/read failures.

- Correct offset-to-EOF mapping lengths and report file mapping errors.

- Persist loading phases without enabling verbose logging.

- Remove mutex-protected network logging and drain waits from signal reports.



## 0.4.8-preview -- development build



- Resolve relative module subdirectories and query loaded module names first.

- Preserve disk-full status and existing save headers on failed writes.

- Count successful renderer-output refreshes separately from swap requests.

- Record build identity and memory-reservation errors for diagnostics.


## 0.4.7-preview -- development build

- Merge duplicate patches after matching the executable version.
- Block conflicting patch writes; activating a conflict in the menu disables the old choice.
- Honor explicit disabled-VSync patch requirements per title without changing saved preferences.
- Refresh native vblank pacing after a patch override.

## 0.4.6-preview -- development build

- Avoid fatal exceptions when persisting profile/content headers during startup.
- Write complete padded metadata through a temporary file; retain old data on failure.
- Keep early startup and fatal-signal reports in installation logs/boot.log when writable.


## 0.4.5-preview -- development build

- Connect launcher patch choices to Canary before guest execution and JIT precompilation.
- Reuse Canary executable hashes for version matching and library metadata.
- Validate memory ranges, verify writes, reject incompatible versions and log applied names.
- Accept Windows line endings in patch choices; report VSync requirements.

## 0.4.5-preview — development build

- Connect launcher patch choices to Canary before guest execution and JIT precompilation.
- Reuse Canary executable hashes for version matching and library metadata.
- Validate patch memory ranges, verify writes, reject incompatible versions, and log applied names.
- Accept Windows line endings in patch choice files; report VSync requirements.

## 0.4.4-preview — development build

- Reduce normal logging to session identification, warnings and errors; disable
  duplicate platform stdout output and hidden debug-file activation.
- One-time reset of detailed logging, with subsequent explicit choices saved.

## 0.4.3-preview — development build

- Restore writable guest `cache:`, `cache0:` and `cache1:` devices in both
  native and host startup. This frontend setup was absent despite desktop
  Canary mounting the same devices. GTA IV logs showed missing cache paths.
- Keep utility cache separate from shader caches, games and saves. A VFS
  regression verifies guest writes, distinct devices and persistence.

## 0.4.2-preview — development build

- Prefer the installation's lowercase `logs` folder for per-game sessions.
- Keep sandbox logging when the title mount is not writable; the desktop helper
  exports those sessions to `/data/homebrew/PPSA50011/logs` and downloads logs
  already exported there even when the title is closed.

## 0.4.1-preview — development build

- Library duplicate detection uses the complete normalized source path without
  relying on native realpath. Different folders containing `default.xex` remain
  separate games; overlapping configured folders still deduplicate.
- Scan logs record every discovered game and directory enumeration errors.

## 0.4.0-preview — development build

- Saved VSync control in the library settings. Changing it restarts the native
  title when the panel closes, before Canary initializes its frame limiter.
- Cover layout follows the supplied Aurora reference: a larger parallel center
  cover, overlapping neighbours receding in size and a floor reflection.
- Regression checks cover VSync persistence/restart and cover overlap/alignment.

## 0.3.0-preview — development build

- Automatic log files per game launch, with game/source identification, UTC
  timestamps, build version and native crash-report routing.
- A preserved first segment and three rotating segments for verbose sessions.
- Configurable game folders with a controller-operated folder picker; offline
  locations persist and overlapping roots do not duplicate games.
- `console.py game-logs` retrieves mounted log files through FTP and packages
  them in a ZIP ready to share.

## 0.2.0-preview — development build

- Projected 3D cases with visible spines and tops, continuous rotation and
  subdivided cover textures for perspective.
- Frame-progress diagnostics now include loading that never submits a frame.
  Captures are bounded and reset when rendering resumes.
- A translated no-frame notice preserves access to the touchpad guide.
- Garden Warfare online-service limitations are documented. This build does
  not implement Xbox Live/EA authentication or claim to fix its loading.

See the [development notes](docs/releases/v0.2.0-preview.md).

## 0.1.0-preview — 2026-10-03

First public preview: Xenia Canary PS5 port, native audio and DualSense input,
RADV Vulkan rendering, game library, profiles, community patches, covers and
the touchpad guide. Includes Noto Sans fonts and English, Portuguese and Spanish
UI translations with automatic console-language selection.

GPU upload and thread-stack changes improved Sonic and Budokai host reproductions.
PS5 gameplay validation and compatibility refinement are ongoing; Burst Limit
and Ultimate Tenkaichi fight black screens remain unresolved.

See the [release notes](docs/releases/v0.1.0-preview.md) for verification limits.
