# Rage Wars Recompiled - v0.1.0-beta.1

**First public beta — Windows x64 desktop PC and experimental PCVR.**
XR64 Studios / non-commercial fan recompilation / build **47385556**.

## Download and setup

Download **RageWarsRecompiled-v0.1.0-beta.1-windows-x64.zip**, extract it and open
**RageWarsRecompiled.exe**. Select your own supported **Turok: Rage Wars US v1.0
ROM** and choose PC. Keep every included DLL beside the executable.

**At the game's start screen, press Escape. Enter and Space do not advance it.**

[Setup](https://github.com/ArtfulRascal8/rage-wars-recompiled/blob/main/SETUP.md) · [Report a bug](https://github.com/ArtfulRascal8/rage-wars-recompiled/issues/new?template=bug_report.yml) · [Known issues](https://github.com/ArtfulRascal8/rage-wars-recompiled/blob/main/KNOWN_ISSUES.md) · [Discussions](https://github.com/ArtfulRascal8/rage-wars-recompiled/discussions)

## Included changes

- Consistent Rage Wars Recompiled executable, window, setup and download naming.
- Existing PC Options connected to pause and title/lobby Options. Pause Options was owner-confirmed on earlier build 39B82289; title/lobby paths passed native lifecycle tests. Complete live menu coverage and persistence remain open.
- ROM-only source reconstruction with pinned public generator/dependency source and explicit integration patches. Snapshot comparison is optional; audio generation preserves ROM/region/handler checks.
- Preview audio repair, portable allowlisted packaging, corrected complete component notices and matching source alongside the binary.
- Dedicated project page, bug reports, Discussions and release comments.

## Tested evidence and limits

On exact executable **47385556**, the owner confirmed normal four-player preview
and one-player/three-bot desktop combat rendering, working controls and good audio.
Local empty-output source reconstruction passed exact comparison of all **60**
guest/audio inputs, the host build, **eight targeted CTest contracts**, consumer
production auditing and declared/read input auditing with **zero unexpected inputs**.
All **3,206** callback definitions were preserved.

The final notice/doc repack retains every tested executable/DLL byte. Package
contracts (**10 tests**) and reconstruction helper contracts (**6 tests**) passed.
Both archives passed full manifest hashes, explicit inventories and ZIP CRC/byte
checks, plus bounded privacy checks. Raw local build/runtime evidence stays private.
These checks do not prove every possible privacy issue or independent OS isolation.

Known beta limits: Escape-only start progression; incomplete live menu/persistence
coverage; inherited missing callback **0x00231F4C** outside tested scenarios;
broader gameplay/save/device-recovery regressions; unaccepted headset attachment,
recenter, tracking-extension support, near-wall and fallback weapon behavior,
barrel/aim-origin agreement and moving-shot impact/damage. Calibrated models need
your own extracted inputs; automatic extraction is unavailable. No Quest APK.
**Independent-machine or donor-denying sandbox validation remains open.**
See [Known issues](https://github.com/ArtfulRascal8/rage-wars-recompiled/blob/main/KNOWN_ISSUES.md).

## Matching source, licensing and provenance

Download **RageWarsRecompiled-v0.1.0-beta.1-source.zip** beside the Windows ZIP.
Its **docs/PC_RECONSTRUCTION.md** explains the pinned Windows toolchain and ordered
rebuild using your own ROM. It includes reviewed port/runtime/generator sources,
patches, configurations and scripts. GitHub's automatic “Source code” archives
contain this documentation repository only; use the explicitly named source asset.

GNU GPLv3 and dated modification/source-access notices are in **COPYING.txt** and
**MODIFICATIONS.txt**. Component licenses and attribution are preserved in both
archives and indexed in **THIRD-PARTY-NOTICES.txt** in the Windows package.
The binary contains translated guest code; source includes ROM-derived
reconstruction edits. No ROM file, extracted assets, saves, profiles, RAM captures
or old Git history are supplied. Original game copyrights remain with their owners.
XR64 Studios has chosen non-commercial fan distribution; this records its decision
and does not claim rights-holder permission, endorsement or a legal finding.

## Build identity

Executable SHA-256:
`47385556a13586971c341e770d7053b1503b668b5f0748b85fcfe56a27edd659`

Version: **0.1.0-beta.1**. This is a successor to selected baseline **F67637BF**.
Download hashes are in **SHA256SUMS.txt**; per-file hashes are in each archive's
manifest. This is an experimental beta with the acceptance boundaries above.
