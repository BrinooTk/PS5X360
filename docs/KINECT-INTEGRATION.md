# Camera motion to guest Kinect: integration status

## Observed target

- Dragon Ball Z for Kinect, title `4E4D084E`, executable hash `8FDD4F4AB147612A`.
- Owner's PS5 firmware: 13.60. Test image: experimental.20.
- GOD source, title screen rendered at approximately 30 output frames/s in the captured session. This is not a gameplay benchmark.
- The owner demonstrated an animated motion overlay. The guest game still waited for a raised hand.
- Research status continues to report `guest_sensor_ready: false`.

## Confirmed initialization blocker

A host reproduction with the installed game's files shows `XamContentOpenFile` requests for `update:\Database.xmplr` and `game:\Database.xmplr`, mounted as `stexemplar`. The current stub always returned file-not-found. The game's root directory contains a 13,168,640-byte PIRS package named `database.xmplr`. This failure occurs before the observed camera service calls.

The research candidate adapts [Xenia Canary PR 1276](https://github.com/xenia-canary/xenia-canary/pull/1276), preserving its BSD license. It mounts read-only STFS packages from guest devices. A file-backed reader with shared, bounded slices avoids copying the full nested package into the console's malloc budget. Package headers are read with `ReadAt`, with magic and data-offset checks. Missing files, directories and malformed packages are rejected. Synchronous and deferred requests use the existing content manager and completion machinery.

The subsequent host run mounted `stexemplar:` and `nuiidentity:` successfully, then tried reading `stexemplar:\database.gmsodf`. It stopped at an undefined `LDICreateDecompression` export. The next candidate also adapts [PR 1275](https://github.com/xenia-canary/xenia-canary/pull/1275), using the existing libmspack LZX decoder, with additional checks of every guest page's access permissions, input size limits and a bounded number of contexts.

These changes supply package access and model decompression. They do not turn phone joints into guest Kinect frames.

## Remaining device work

The loaded image imports `PsCamDeviceRequest` and contains the Kinect SDK. A caller forwards a request block to the kernel, after trying `XamXStudioRequest`. [PR 1274](https://github.com/xenia-canary/xenia-canary/pull/1274) documents camera request layouts derived from a different title and supports physical Kinect v1 depth/Bayer streams on Windows. That implementation is a useful reference, not evidence of phone-camera support on PS5.

- Validate the Dragon Ball request codes and required initialization services after mounting its data package.
- Implement the guest frame contract, callbacks, tracking identity, calibration and loss of tracking.
- Decide between guest SDK depth processing and a verified skeleton-level adapter. An RGB camera's estimated joints are not measured depth or an infrared stream.
- Only advertise a ready guest sensor after its required services are implemented and tested.

Host inspection outputs and privately copied game files remain under ignored `build/`; neither belongs in releases or AutoLog.

## Additional host evidence (experimental.21)

- The nested package lifecycle check passed: mount, duplicate rejection, close/unmount, invalid executable and missing package rejection.
- The production LDI decoder passed a known uncompressed LZX block, reset, bounds guards and invalid-size cases under ASAN/UBSAN.
- The real game then executed its LDI decompression calls and destroyed the decoder without an LDI error. It proceeded to NUI identity files, but produced no output swaps in the 35-second run.
- The two constrained physical allocations were retried successfully by the game with unconstrained allocations; they are not established as the remaining blocker.
- A debugger capture found the main guest thread waiting in `KeWaitForSingleObject`. The initialization path also calls the missing `KeInitializeMutant` export. The next diagnostic adapts [Xenia PR 2366](https://github.com/xenia-project/xenia/pull/2366), using Canary's existing 32-byte `X_KMUTANT` layout, not the smaller structure in the donor patch.

The candidate is held from console installation until this new initialization regression is resolved. The installed experimental.20 remains intact. No raised-hand recognition or Kinect gameplay has been confirmed.

## Embedded mutant fix result

The production embedded mutant test passed recursive acquisition, cross-thread exclusion, one-level release still excluding the other thread, and complete release permitting acquisition. After this correction the real game submitted 428 swaps in a 25-second host run. This removes the no-swap initialization regression above; these counts are functional evidence, not a PS5 performance measurement. Detailed sensor tracing and console validation remain pending.

The detailed follow-up trace produced 332 output swaps in 20 seconds and showed the game now using `KeInitializeMutant`/`KeReleaseMutant` in its NUI identity initialization. It still used unavailable NUI user binding, camera tilt callback and identity/cache/biometric services. No runtime `PsCamDeviceRequest` call was observed in this host trace. Next console validation should establish whether the same missing service path prevents reaching the camera request dispatcher. Neither a valid phone pose nor a rendered game frame proves that the guest consumes a skeleton.

## Console result after experimental.21

The real PS5 trace `console-net-20261006T221004.131120Z.log` reached `PsCamDeviceRequest` code 0, from the NUI worker. Unlike the host trace, this confirms that the console game reaches the camera state query after the initialization fixes. The export still returns `X_STATUS_UNSUCCESSFUL`: no virtual camera/frame backend exists. The owner's raised-hand test consequently did not advance the title screen. This is an incomplete guest integration, not a tracking-quality or framerate diagnosis.

A later check of the loopback relay returned HTTP 403. This confirms its current pairing was rejected, not whether frames had been arriving during the earlier gameplay attempt. The relay now clears a rejected key and reports `pairing_expired`; the client distinguishes that from an unreachable console. No automatic key discovery or device-ready claim is added.

The next required implementation is the guest camera/frame contract or a verified guest SDK skeleton adapter. A code-0 ready response alone cannot supply frames, tracking identity, completion callbacks or the skeleton consumed by the game. Do not ask testers to repeat the raised-hand test as a feature validation until that bridge exists.

## Virtual camera implementation (experimental.22 candidate)

The next candidate replaces the unavailable PsCam stub with a virtual driver
based on PR 1274's request contract. It rasterizes current estimated joints into
640 × 480 depth, generates empty depth after tracking loss, and implements
asynchronous reads with guest PowerPC completions. Driver state, synchronous
version/calibration/table/timing queries and stationary tilt status are covered.
Color is synthetic neutral gray; no webcam recordings are forwarded.

The production depth renderer passed ASAN/UBSAN checks for frame size, blank
tracking loss, changing arm geometry and exact packed 11-bit roundtrip. The
guest ABI test and real game validation are separate gates. SDK skeleton
recognition and in-game player binding remain unconfirmed until observed.

## Why the game ignored the player (experimental.23)

Reported symptom, owner's PS5, experimental.21: the overlay follows the player,
Dragon Ball Z for Kinect does not react.

Confirmed on the host runner by tracing the game and reading its code:

- The single `PsCamDeviceRequest` (code 0) seen on the console was part of a
  **shutdown**. The game's NUI initialization (function at `82493470`) failed at
  its last step: `XMsgInProcessCall(0xFE, 0x0002C009, &callback)`, the identity
  callback registration, got the emulator's generic failure for unknown XAM
  messages. The game then resumed its camera thread only to let it exit. Trying
  other device states could not help; the earlier "missing services" list was
  not the cause either.
- With `0x0002C009` accepted the game configures the camera (requests 27, 35,
  24, 33, 13, 14, 25) and reads depth and colour frames (request 5).
- The game's body tracking then runs on the GPU: depth as a 320x240 `k_16`
  texture, tree tables as 8192-wide 1D textures, 160x120 `k_16_16` targets.
  Each sensor frame ends in eight resolves with copy command 2 and one
  copy-mode draw of 1800 vertices (600 rectangles). The emulator refuses both
  ("Unsupported resolve copy command 2", "Unsupported resolve vertex buffer
  format"). The skeleton frames the library publishes therefore never hold a
  body. What copy command 2 writes is not known.

Decision: the skeleton-level adapter. `NuiSkeletonGetNextFrame` of the title
(`82495EE0` in this executable; the game calls it every frame with a 10 ms
wait) is found by signature and bound to a host handler that returns a
`NUI_SKELETON_FRAME` (0xAB0 bytes, six 0x1C0-byte skeletons from offset 0x30)
built from the bridge's pose. Field values for an unrecognized player come from
the library's own code: enrolment index -1, user index 0xFE. See
`include/xbox360ps5/virtual_skeleton.hpp` and `BindSkeletonSource` in
`xboxkrnl_pscam.cc`.

Host result with a fixed test pose: function bound, 1381 frames taken by the
game in 240 s, all with a body. This shows the game asks for and receives the
frames; it does not show that the game accepts the player.

Open, in the order they would block:

1. Console observation of the game with a real pose (experimental.23).
2. Whether the game wants more than the skeleton: it also reads three image
   streams each frame, and compares the enrolment index with -1, -2, -4 and -5.
3. The pose is hip-relative; the body does not move across the room.
4. Hypothesis, not checked: the depth renderer places a limb that is nearer the
   camera farther away (`2.5 + z`), the opposite of the skeleton path. It only
   matters for pictures the game draws from the depth stream.
5. The game's own tracking still runs and is wasted work; its console cost is
   not measured. Feeding it empty depth is a candidate if it is expensive.
6. Phone transport: the camera page needs HTTPS on a phone; the relay is
   loopback-only.

A title with optional Kinect must not run with camera motion on: Forza Horizon
(`4D5309C9`) stayed on its loading screen on experimental.21 after
`XamContentOpenFile` mounted its speech package. That export is a "file not
found" stub again unless camera motion is on.
