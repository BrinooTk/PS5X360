# Compatibility — preview 2026-10-03

PS5X360 is functional on the development console, but **game compatibility is still being refined**. A boot screen or menu is not evidence of complete gameplay compatibility.

## Evidence and known issues

| Area / game | Observed state | Remaining verification |
| --- | --- | --- |
| Native PS5 application | Games have run on the development PS5, firmware 13.60 | Broader console and firmware coverage |
| Sonic the Hedgehog (2006) | Black screen at the transition into gameplay was reported. New GPU-upload and stack-recommit changes reached Soleanna in a host reproduction | Full gameplay confirmation with the new build on PS5 |
| Dragon Ball Z Budokai HD Collection | Sub-title launch handling was improved; new GPU uploads removed green/cyan corruption in a host reproduction | Budokai 1 and 3 gameplay and title switching on PS5 |
| Dragon Ball Z Burst Limit | Black screen reported during fights | Diagnosis and a confirmed fix |
| Dragon Ball Z Ultimate Tenkaichi | Black screen reported during fights | Diagnosis and a confirmed fix |
| Guide animation, fonts and languages | Current native build compiled, image validated and installed; language mapping and font atlas tests passed locally | Visual check and automatic language detection after reopening on PS5 |

These are regression notes, **not a complete supported-games list**. Host tests and hardware results are kept distinct. Other games may boot, fail or have audio/graphics issues.

## Performance

Frame rate varies with the title, scene, shader compilation and enabled patches. No universal 60 FPS claim is made. CAS/FSR are presentation filters; they do not guarantee higher gameplay frame rate.

## Help improve compatibility

Report the game title and title ID, region/version, exact failing scene, release version, firmware and enabled patches. A short video and relevant log excerpt help reproduce the failure. Never attach ROMs or game files to an issue.
