# Loading and cover regression checks

Per-game files and multi-folder configuration are described in
[Installation and usage](INSTALLATION.md). Storage regression checks run with:

```sh
clang++-18 -std=c++20 -pthread -Iinclude tools/check-game-storage.cpp -o build/check-game-storage
build/check-game-storage
```

## Frame-progress monitor

The monitor lives in `include/xbox360ps5/frame_watch.hpp` and is called once per
second from the native title loop. Its tests do not require game files:

```sh
clang++-18 -std=c++20 -Iinclude tools/check-frame-watch.cpp -o build/check-frame-watch
build/check-frame-watch
```

Checks cover initial zero-frame loading, the bounded diagnostic window, recovery
before and after the second capture, normal progress, and finite projection
coordinates throughout the cover flow.

For a real failure, record the title ID/version, patches, exact loading screen,
elapsed time and the complete `STALL` section of the log. Two passes show the
guest registers, suspend counts and host thread probes. Differences between
the snapshots are evidence of progress; unchanged snapshots alone do not prove
deadlock. The brief verbose trace supplies additional context about guest calls.

```sh
python tools/console.py netlog --host 192.168.0.19 --seconds 180 --follow
```

Open the emulator and reproduce the failure while this command is running.
It saves the stream in `build/console-net.log`. Game selection and controls
remain manual; this procedure does not cycle or pause games automatically.

## Offline launcher preview

Use the **disposable Docker container** below. The preview creates synthetic
fixtures at `/app0/assets/roms` inside the container; it should not run directly
on a machine whose `/app0` contains real game data. No game executes and no
GPU is required. The software renderer rasterizes the actual launcher's ImGui
draw list with generated grid textures. Its nearest sampling and raster edges
are an approximation of the native renderer, not a PS5 screenshot.

```sh
cmake -S canary -B build/canary-runner
cmake --build build/canary-runner --target xbox360ps5-launcher-preview -j 4
mkdir -p /app0/assets/fonts
cp dist/PPSA50011/assets/fonts/*.ttf /app0/assets/fonts/
build/canary-runner/xbox360ps5-launcher-preview build/library3d-preview.png
```

Run these commands inside the existing `xbox360ps5-canary` build image with
the project mounted. Inspect the image for visible tops/spines, continuous
texture grids and unobscured controls. Native appearance still needs hardware
verification after reopening the title.
