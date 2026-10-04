# Automatic diagnostic reports

## Console ELF (preview)

`PS5X360-AutoLog.elf` is now available as a separate console collector. It reads
per-game logs directly on the PS5 and has the owned relay preconfigured. Testers
load it once per boot, then reports are sent without a PC collector. See
[`native/autolog/README.md`](../native/autolog/README.md) for the reporting notice,
opt-out marker and exact validation limits. Direct PS5-to-Discord delivery was
verified on the owner's firmware 13.60 console with its FTP service enabled.
Other firmware remains unverified. The PC method below remains an optional
development fallback and must not run concurrently when validating console-only
delivery.

AutoLog 1.0.7 sends one activation check through the same console HTTPS upload
path and retries failed attempts. The developer receives the version and
initial directory status even if game log listing fails. This verifies delivery
only, not game capture. Game reports still wait for session events. See the
collector README for updating an already running process and opting out.

## Server

PS5SX2's separate installer/logger monitors its app and uploads reports to a
Cloudflare Worker, which forwards them to a private Discord channel. PS5X360's
independently written relay uses that architecture. The initial collector runs
on a Windows PC using existing PS5 FTP logs; **it is not a console background ELF**.
For this PC fallback the PC must stay on. No emulator executable, game timing,
patches or FPS settings are changed by this tool.

You need a Cloudflare account, Node.js and a private Discord text channel.
There is no need to open router ports or host the web server on your PC.
The generated `https://ps5x360-logs.<account>.workers.dev` address is sufficient;
a custom domain can later be attached in Cloudflare's Worker settings.
Check Cloudflare's current plan limits before opening community-wide uploads.

1. Create a **private** Discord text channel, such as `ps5x360-logs`.
2. Channel Settings > Integrations > Webhooks > New Webhook. Copy its URL.
3. Double-click `server/autolog/deploy.cmd`. Sign in to your own Cloudflare account
   in the browser. The script requests the webhook locally as the
   `DISCORD_WEBHOOK_URL` Worker secret. Never paste it in chat or commit it.
4. Set the `UPLOAD_TOKEN` secret to a random password of at least 24 characters
   from your password manager. Save it for collector configuration.
5. The script tests and deploys the Worker. Copy its HTTPS URL and append
   `/v1/reports`. Do not use PS5SX2's developer's endpoint.

If the Worker and upload token are already provisioned, use
`server/autolog/set-webhook.cmd` to set only the webhook. Owner automation is
available through `tools/provision-autolog.py`; it creates the upload token
locally, puts it into Cloudflare and leaves the collector disabled. It does not
contain a webhook and must not be run by testers against the owner's account.

Deployment may require Cloudflare account setup or a `workers.dev` subdomain
selection. A successful dry run is not a live deployment. `/health` only tests
reachability; it does not confirm successful Discord delivery.

## Collector on each tester's PC

Requirements: Python 3.10+, PS5 FTP enabled on its usual LAN port 2121, the
developer's relay endpoint and upload token. Testers choose whether to send logs.

1. Double-click `tools/Configure AutoLog.bat`. Enter the PS5 LAN IP, relay URL and
   upload token (hidden prompt). Type `YES` to enable uploads.
2. Double-click `tools/Start AutoLog.bat` and leave it running while testing.
   If the developer already provisioned this PC's endpoint and upload token,
   use `tools/Enable AutoLog.bat` and type `YES` instead of re-entering credentials.
3. Play normally. Logs under `/data/homebrew/PPSA50011/logs` are checked every
   30 seconds. Files unchanged for two minutes are sent as **snapshots**, not as
   confirmed completed sessions. Later changes can generate another snapshot.
   Up to the 30 latest retained game sessions, including earlier sessions, are
   eligible on first start. Launcher-only logs and unrelated files are excluded.
   Stability uses FTP `LIST` sizes/timestamps for compatibility with PS5 FTP
   payloads. Timestamp precision depends on that server; same-size rewrites within
   its timestamp resolution may not be detected. This is best-effort collection.
4. Reports appear as text attachments in the private channel with a checksum ID.
   Local queue data is in `%LOCALAPPDATA%/PS5X360-AutoLog/outbox.sqlite3`.

`boot.log` is deliberately excluded: existing builds do not provide a reliable
session ID linking it to each game. A freeze preventing FTP access delays
collection until FTP is available again. Reports cannot recover logs never
written to disk or files rotated away while the collector was offline.

## Privacy, retention and failure handling

- No ROMs, saves, profiles, full game list or unrelated directories are uploaded.
- Game name, title ID, build, diagnostic lines and crash offsets remain useful
  to the developer. Source paths, credential fields, URLs, IPv4/IPv6 addresses,
  MAC addresses and common PC username paths are redacted. Redaction is best
  effort; logs can contain unexpected personal information. Tell testers what
  is sent and who can access the receiving channel before enabling collection.
- Credentials are saved in the user's local `config.json`, not the source tree.
  Protect this folder as you would other credentials. The upload token permits
  report submission; it cannot read the channel or replace the webhook.
- Queue: at most 20 pending reports, each at most 2 MiB. When full, collection
  waits and leaves console logs untouched. Reports retain initial context and
  recent diagnostics, omitting the middle of large segments.
- HTTPS verifies the relay certificate/hostname and forbids redirects. Failed
  uploads remain queued with exponential retries (30 s to 30 min).
- A report is removed from the local queue only after the relay confirms that
  Discord acknowledged delivery. Rare duplicates are possible if the response
  is lost after Discord accepts a report; compare the report checksum IDs.
- Set `enabled` to `false` in local `config.json`, or create an empty local
  `no-log-upload` file, to stop collection and sending. Ctrl+C stops this run.
  Pending reports remain on the PC and may be sent if you enable uploads again.
- Server: authenticated upload token, 2 MiB request limit, SHA-256 validation,
  six submissions per minute per network address, Discord mentions disabled.
  Rotate the token through Wrangler if it is shared outside the tester group.
- Relay stores no log archive; Discord channel retention is controlled by its
  owner. Cloudflare processes request metadata as part of the hosting service.

## Verification

`node --test` in `server/autolog` checks forwarding with a mocked Discord receiver,
authorization, size/hash checks, rate limiting and failures. `python
tools/check-autolog.py` checks stability, game selection, privacy filtering,
persistent retries, disabled uploads, acknowledgement validation and queue caps.
The Miniflare test exercises the actual Worker runtime with a mocked receiver,
including multipart attachment forwarding. The Worker checks manual redirect
responses; the edge runtime does not implement Fetch's `redirect: "error"`.
These do not prove a live PS5 -> Cloudflare -> Discord delivery. Verify that path
after deploying to the owner's account, using a harmless test session first.
`python tools/check-relay-live.py` sends a single synthetic setup report through
the owner's locally configured relay, with no console logs or personal data.

Reference: https://github.com/Swordpdf/PS5SX2/tree/main/ps5/installer
Cloudflare secrets: https://developers.cloudflare.com/workers/configuration/secrets/
Rate limiting: https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/
