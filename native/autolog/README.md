# PS5X360 AutoLog ELF 1.0.9-preview

This revision retries both supported local FTP ports even if the first server
accepts a connection but refuses a command. It uses CWD + LIST for servers
that do not support absolute LIST paths. Directory failure status now includes
the direct errno and the FTP stage/reply for both ports. The FTP service still
must be enabled by the console owner; this collector does not enable it.

Earlier revisions fixed startup for both SDK loader argument forms, clear BSS and
keeps the ELF entry point nonzero. It also adds a loopback directory fallback,
initializes the DNS memory pool, and avoids the socket `fcntl` call denied in
the tested payload environment. The kernel/privilege CRT is excluded.
The reliable runtime diagnostic is `PPSA50011/autolog/status.txt`; a visible
notification or startup log is not required to confirm collection and delivery.

The separately distributed collector executable includes this SDK adapter under
GPL-3.0-or-later, copyright John Törnblom. Its complete adapter source and license
are included. The emulator is not linked to this collector. Independently written
collector files retain their MIT notices; MbedTLS retains its own license.

This separate collector runs on the PS5. Its intended route is:

**PS5 game logs → verified HTTPS → Brino's Cloudflare relay → private `auto-log` Discord channel.**

A PC is not needed to collect or forward reports. Testers do not configure an
account, Discord webhook, relay address or password. The ELF has the developer's
report endpoint and a limited submission credential built in. This credential is
recoverable from the distributed client and grants report submission only.
The Discord webhook and the owner's upload credential remain server-side.

## Automatic activation confirmation

When loaded, this version sends a small **AutoLog activation check** report
through the same verified HTTPS endpoint and public console credential used for
game reports. It includes the collector version and directory scan status, not
game logs, games or saves. It is sent once per collector process; failed attempts
retry with backoff. Successful game reports still require a session event.

The activation check does not require FTP directory listing to succeed. This
lets the developer see that upload works even when a tester cannot access logs.
It confirms transport only: the directory status must also succeed for game
capture. If no activation arrives, runtime startup or network delivery remains
unconfirmed on that console. The last accepted report ID and acknowledgement
are retained in `autolog/last-delivery.txt`.

## Tester instructions

1. Install the PS5X360 emulator normally. Use a build that writes per-game logs
   to `/data/homebrew/PPSA50011/logs`.
2. Read the reporting notice below, then load `PS5X360-AutoLog.elf` with your
   existing SDK-compatible ELF loader **once after each console boot**.
   Keep the console's existing FTP service enabled (port 2121 or 1337) if its
   POSIX directory API refuses access. The collector uses FTP over loopback
   (`127.0.0.1`) only as a directory metadata fallback. It does not enable FTP,
   open router ports, require a PC collector, or change process privileges.
3. Play and test games normally. The collector stays in a separate process.
   With Internet access, ended sessions and recorded crashes are forwarded automatically.
   Loading another copy does not start a second collector.
4. Reports are queued when delivery fails and retried with backoff. After a
   console reboot, load the ELF again to resume. It installs no boot service.

The emulator itself still needs to be installed: this ELF is a logger, not an
emulator installer. Follow your loader's ordinary loading procedure; do not use
private runtime-loader ports or repeatedly inject into an occupied loader.

### Updating an already running collector

Replacing the ELF file does not replace a process already running. Create
`/data/homebrew/PPSA50011/no-log-upload`, wait until `autolog/status.txt` says
stopped, then remove the marker and load the new ELF. Normally this takes about
30 seconds; an in-flight network operation may take longer. Do not reboot the
console solely to update the collector.

### Reporting notice and opt-out

Loading this diagnostic ELF enables automatic sharing of game diagnostic logs
with the project's developer, including an activation connectivity check and
directory access diagnostics. A console notification announces reporting.
Reports contain the game name, emulator build and diagnostic log excerpts.
Games, saves, profiles, the complete library and unrelated application logs are
not read or uploaded. Lines containing credentials, addresses, URLs or private
source paths are removed on a best-effort basis. Other information printed by a
game may remain. Cloudflare and Discord process these reports; the receiving
channel is private, but the developer can read and retain its attachments.

To stop and prevent further uploads, create this empty file on the console:

`/data/homebrew/PPSA50011/no-log-upload`

The collector checks it between operations and immediately before sending.
An already submitted request cannot be recalled. Keep the marker in place
across boots; remove it and load the ELF again only when you want to resume.

### Files and delivery

- Original game logs remain in `PPSA50011/logs`.
- `PPSA50011/autolog/NOTICE.txt`: reporting notice.
- `PPSA50011/autolog/status.txt`: latest collector status, PID and check time.
- `PPSA50011/autolog/last-delivery.txt`: last exact Discord receipt and report ID.
- `PPSA50011/autolog/outbox`: at most 20 bounded reports, up to 20 MiB total.
- `*.seen`: session snapshot fingerprints to suppress repeats.

Only sessions started after the persisted `autolog/collect-after.txt` UTC epoch
are considered (also limited to the last 24 hours, up to 30 per scan). On first
activation, this cutoff defaults to the activation time. Existing historical
sessions are excluded even if their logs receive new lines.
Launcher-only logs and `boot.log` are excluded: existing builds don't reliably
associate boot logs with a game session. Silence in a log no longer triggers an upload. The collector requires a recorded
native/guest crash, an explicit ENGINE EXIT/RESTART line, or a later emulator
session. The reason is included in the report. After this event, logs must remain
unchanged for 30 seconds so final diagnostic lines can be captured. Each session
is queued once. Forced closure without a recorded event is detected only when
a subsequent emulator session appears. A freeze while the process remains
active is not automatically classified as a crash; close/reopen the emulator
to submit that session. A whole-console freeze/power loss delays delivery until
the collector and emulator are started again.
The first context and recent excerpts of each segment are retained. Reports
are capped below 1 MiB and deliberately do not contain the entire verbose log.

A queued report is removed only after the relay returns the matching SHA-256
receipt **after Discord acknowledges the attachment**. Lost replies can cause
a duplicate attachment. A full queue leaves the console's source logs untouched.
TLS requires a trusted certificate, correct hostname and valid console clock;
there is no unencrypted fallback. Blocked Internet DNS or HTTPS prevents delivery.

## Current verification

- Version 1.0.9 preserves `guest kernel crash` classification when a
  `KeBugCheck` marker is followed by a guest register dump. This is a diagnostic
  improvement; it does not resume or repair a failed guest kernel.

- Version 1.0.8 recognizes `Guide: back to the launcher` as normal session
  completion. Host regression tests also cover live-session exclusion, opt-out,
  privacy redaction and queue deduplication. This version requires a console test.

- On the owner's PS5 (firmware 13.60), version 1.0.7 sent its activation check
  without a manual queue insertion. Discord returned HTTP 200 and the exact
  receipt `6cb52f5f3bc1e09027c459e3885905a359f01ebb9518224571791802bd660389`.
  The PC uploader remained off. This does not validate external consoles.
- FTP regression: a server on port 2121 that refuses login falls back to 1337;
  CWD + LIST works when absolute LIST is unsupported. Names with spaces are retained.
- Cross-compiled x86-64 PS5 ELF with 16 KiB segment alignment.
- Separate minimal userland startup; no SDK kernel patch/privilege CRT linked.
- Native code tested on a host with AddressSanitizer/UndefinedBehaviorSanitizer:
  redaction, digest, opt-out, atomic queue and unchanged-snapshot suppression.
- Native MbedTLS transport delivered a synthetic report to the owned live relay
  from a host. This verifies native HTTPS and relay integration, **not PS5 execution**.
- On the owner's PS5 (firmware 13.60), the native collector read actual game
  logs, queued reports and sent a GTA IV report directly through verified HTTPS.
  The relay returned HTTP 200 and the matching receipt after Discord accepted
  the attachment. The private channel independently showed the report
  `975f543b575f13cdf2a85d42133a12ee8b38e98cd8fa93a807261e9d7d854b98`.
  The PC collector was stopped throughout this console test.
- Other firmware, reboot cycles and persistence across emulator exits remain
  unverified. Do not describe this preview as validated on every firmware.

## Build/source

The archive includes collector/startup source, the generated CA/relay headers,
the exact MbedTLS 3.6.7 source archive and its licenses. The generated submission
key is intentionally distributable; never substitute a Discord webhook for it.

Use a PS5 Payload SDK-compatible toolchain, set `PS5_PAYLOAD_SDK`, and run
`tools/build-native-autolog.sh` from the project root. Restore the packaged
generated headers into `build/autolog-native` and extract the MbedTLS archive
into `.deps/autolog` first. Host checks use `tools/check-native-autolog.sh` with
Clang 18 and sanitizers. Server provisioning is owner-only.

MbedTLS source SHA-256:
`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`

Architecture reference: [PS5SX2 installer/logger](https://github.com/Swordpdf/PS5SX2/tree/main/ps5/installer).
The collector is independently implemented and does not use PS5SX2's endpoint.
