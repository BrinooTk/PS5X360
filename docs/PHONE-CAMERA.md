# Optional Kinect camera relay

Smartphone Kinect is in testing, with targeted identification and menu support for
Dragon Ball Z for Kinect. Other games and combat gestures are not guaranteed.
The PC must stay running on the same LAN as the phone and PS5. No video is uploaded.

## PC webcam

Install Python 3.10 or newer. From this extracted folder, run:

```powershell
python relay/server.py --console YOUR_PS5_LAN_IP --console-port 8360
```

Open http://localhost:8780 on that PC. Enable camera motion on PS5, use the six-character
key shown by its settings page, connect, and grant webcam permission. Use the actual
settings-page port if it is 8361 through 8367 instead of 8360.

## Smartphone camera

Use a fixed PC LAN IP. Install OpenSSL on the PC or use the project's existing Docker
SDK image. Replace both placeholders below with your own private LAN IPv4 addresses:

```powershell
python setup.py --pc-ip YOUR_PC_LAN_IP --console-ip YOUR_PS5_LAN_IP --console-port 8360
.\start-relay.ps1
```

Setup generates your own local CA and server certificate outside the served directory.
It does not install trust or firewall rules. Allow the chosen Python relay on your
private network if the firewall blocks it; do not expose it to the Internet.

Transfer only `runtime/phone-camera-ca.crt` to your phone. On Android install it as
a CA certificate through Security settings. On iOS install its profile, then explicitly
enable full trust in Certificate Trust Settings. Compare its fingerprint to
`public/certificate-receipt.json`. Never share `runtime/keys/relay.key`.

Copy `public/phone-camera.conf` into `/data/homebrew/PPSA50011/phone-camera.conf`.
Copy `runtime/phone-camera-ca.crt` into
`/data/homebrew/PPSA50011/assets/phone-camera-ca.crt`. Restart the emulator if needed.
The PS5 QR settings page's Camera tab can now open your HTTPS phone relay.
Alternatively open `https://YOUR_PC_LAN_IP:8781/` directly. Enter the PS5 pairing key,
connect, select front or back camera and grant camera permission.

Keep the phone page visible and the screen awake. Certificates expire: server leaf
30 days, CA 90 days. Follow setup's refusal to overwrite existing keys; renew in a
fresh extracted folder and deliberately update the trusted certificate/configuration.
Do not bypass certificate errors.

## Calibration

Original mapping works without calibration. For estimated depth/lateral placement,
enter your height, starting distance and camera height, click Calibrate and stand
still for two seconds with the full body visible. Keep the camera level. Stabilization
is optional and off by default. Original mapping restores the earlier behavior.

Depth is estimated from one RGB camera and can drift; it is not a depth sensor.
Calibration stays in page memory only. Recalibrate after moving/changing the camera,
changing zoom or changing person. See CALIBRATION.md for operating limits.
