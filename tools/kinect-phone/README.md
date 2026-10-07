# Motion research bridge

This tool can send 20 estimated joints from a local PC camera to the PS5X360 research receiver. It does not provide a working Kinect device to games.

## Run

Enable Settings > System > Settings page for a phone on the console. Enable Controls > Camera motion (research), preferably for the target game only. Start `python tools/kinect-phone/server.py --console 192.168.0.19` on the PC (replace the IP for other consoles). If the settings page uses another port, pass `--console-port 8361` (8360–8367). Open http://localhost:8780. Enter the six-character key displayed on the PS5, click Connect PS5 and Start camera. Keys are kept in relay memory only. The console key changes after restarting the emulator.

The PC relay binds only to 127.0.0.1 and forwards only joint data to one private LAN address. The browser never sends camera video. Stop, Disconnect or hiding the page stops tracking/transmission; the PS5 expires the last pose after 500 ms. Do not expose this development relay to the Internet. Pairing grants access over plain LAN HTTP; it is not encrypted transport.

For camera-only use, start the ordinary static server as before and skip Connect PS5. On a phone, trusted HTTPS is required for camera access. This loopback PC relay is not a phone transport; that remains a separate implementation step.

## Protocol

`XMP1`: 4 magic bytes, sequence uint32 LE, nonzero sender session uint32 LE, tracked flag uint32 LE, then 20 joints × (x,y,z,confidence) float32 LE = 336 bytes. Coordinates are hip-relative estimated metres with Y-up/Z-forward; +/-4 bounds, confidence 0–1, no NaN/Infinity. Head, hands and spine are approximate. No sensor-space distance, Kinect depth/color frame, floor plane or audio is implied. A lost body is sent explicitly with tracked=0. Receive time uses the console's monotonic clock.

The native overlay confirms delivery. `GET /api/motion/status` requires the console pairing key and reports enabled/tracked/packets/sequence/age_ms/guest_sensor_ready. That final field remains false. Never advertise the Kinect device as ready until the required guest interfaces and frame layout have been implemented and tested.

The native receiver has no backlog, writes no pose history and runs through the existing web settings service. Non-Kinect games keep the existing device behavior when research is off.

References: https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/web_js and https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia

If the console restarts and rejects the old key (HTTP 403), the relay clears the pairing and the browser reports an expired pairing. Enter the new console key and reconnect. HTTP 502 instead means the console endpoint is unreachable; check that the emulator is open and that the port matches its settings page. Reconnecting restores research pose delivery only, not guest Kinect gameplay.

## From experimental.23

The console hands the received pose to the game as its Kinect skeleton when it
can find the game's skeleton function (see `docs/KINECT-INTEGRATION.md`). The
page sends 30 poses a second and shows what the game is doing with them.
`guest_skeleton_bound`, `guest_skeleton_frames` and `guest_skeleton_bodies` in
the status say whether the function was bound, how many frames the game took
and how many of them held a body. `guest_sensor_ready` still reads false: the
depth, colour, identity and speech sides of the sensor are not emulated.

Turn Camera motion on in the settings of the Kinect game, not in the general
settings.
