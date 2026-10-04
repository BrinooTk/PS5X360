# Compatibility — preview 2026-10-03

PS5X360 is functional on the development console, but **game compatibility is still being refined**. A boot screen or menu is not evidence of complete gameplay compatibility.

## Evidence and known issues

| Area / game | Observed state | Remaining verification |
| --- | --- | --- |
| Native PS5 application | Games have run on the development PS5, firmware 13.60 | Broader console and firmware coverage |
| PS5 firmware 10.20 / 11.60 | Community reports of whole-console freezes when loading GTA IV | Affected-console logs and reproduction; compatibility unverified |
| Halo 3 | Community reports an application crash | Release, firmware, exact stage and execution log not supplied |
| GTA IV | Development PS5 log confirms missing `cache:` and `cache1:` devices; v0.4.3 restores utility-cache mounts globally | Gameplay/loading validation after correction; no 60 FPS guarantee |
| Left 4 Dead 2 | Screenshot shows guest KeBugCheck, STOP 0, after rendering its title screen | Guest call site and preceding failure need execution logs |
| Sonic the Hedgehog (2006) | Black screen at the transition into gameplay was reported. New GPU-upload and stack-recommit changes reached Soleanna in a host reproduction | Full gameplay confirmation with the new build on PS5 |
| Dragon Ball Z Budokai HD Collection | Sub-title launch handling was improved; new GPU uploads removed green/cyan corruption in a host reproduction | Budokai 1 and 3 gameplay and title switching on PS5 |
| Dragon Ball Z Burst Limit | Black screen reported during fights | Diagnosis and a confirmed fix |
| Dragon Ball Z Ultimate Tenkaichi | Black screen reported during fights | Diagnosis and a confirmed fix |
| Plants vs. Zombies Garden Warfare | User reports loading freezes at 0 FPS. The game requires online services; the current core has stubbed authentication/network functions | No local reproduction or confirmed fix. Xbox Live/EA authentication is not implemented |
| Guide animation, fonts and languages | Current native build compiled, image validated and installed; language mapping and font atlas tests passed locally | Visual check and automatic language detection after reopening on PS5 |

These are regression notes, **not a complete supported-games list**. Host tests and hardware results are kept distinct. Other games may boot, fail or have audio/graphics issues.

Native module-load errors and guest kernel crashes are tracked separately in
[the firmware/crash investigation](FIRMWARE-CRASH-REPORTS.md). A screenshot does
not establish a shared firmware cause or a fix. No Xbox 360 BIOS is required.

## Performance

Frame rate varies with the title, scene, shader compilation and enabled patches. No universal 60 FPS claim is made. CAS/FSR are presentation filters; they do not guarantee higher gameplay frame rate.

## Loading at 0 FPS

Zero FPS means no new guest frame was submitted. It does not identify whether
the game is loading, compiling shaders, waiting for a service, or deadlocked.
The development build monitors initial loading as well as later rendering
stops. Initial zero-frame loading triggers a diagnostic capture after 45 seconds;
a later stop triggers one after 15 seconds. Two thread snapshots and a three-second
verbose log window are captured once per stop. A notice appears after 45 seconds
without a new frame, and clears if rendering resumes. These are diagnostic
thresholds, not forced timeouts on guest operations.

The guide remains available through the configured shortcut. The monitor does
not skip guest waits, fake FPS or automatically cycle through games.

Garden Warfare depends on online services. The core's `xam_net.cc` still contains
stubbed online functions: `XNetLogonGetMachineID` returns `0x80151802` (not logged
on), and the title address does not represent an authenticated Xbox Live session.
Working cover downloads do not provide game authentication. Without this game's
execution log, online waiting remains a hypothesis for the reported freeze,
not a confirmed root cause. See [EA's game page](https://www.ea.com/games/plants-vs-zombies/plants-vs-zombies-garden-warfare).

## Help improve compatibility

Report the game title and title ID, region/version, exact failing scene, release version, firmware and enabled patches. A short video and relevant log excerpt help reproduce the failure. Never attach ROMs or game files to an issue.
