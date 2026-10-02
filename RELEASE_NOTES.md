# Rage Wars Recompiled - v0.1.0-beta.1

**Private draft for owner inspection. Runtime menu/gameplay acceptance is pending.**

Windows x64: desktop PC and experimental PCVR in one executable. This is a successor to the selected **F67637BF** baseline, not a filename-only copy.

## Changes in this candidate

- Consistent project, executable, setup window, game window, and Windows file-property naming.
- Existing native PC settings connected to title/lobby Options while retaining pause-menu access. Native captured-menu tests passed; live menu coverage and visual acceptance remain open.
- Personal build-path strings removed from consumer diagnostics, with a repeatable package scan.
- Portable allowlisted runtime package, file manifest, checksums, setup guide, Issues, and Discussions.

## Download and setup

Download **RageWarsRecompiled-v0.1.0-beta.1-windows-x64.zip**, extract it, and open **RageWarsRecompiled.exe**. Select your own supported **Turok: Rage Wars US v1.0 ROM** and choose PC. VR requires an active OpenXR runtime and has the limitations below.

No ROM, original game assets, extracted models, saves, settings, game source, debug symbols, or private archives are included. GitHub's automatic source archives contain this documentation repository only.

[Setup guide](https://github.com/ArtfulRascal8/rage-wars-recompiled/blob/main/SETUP.md) · [Report a bug](https://github.com/ArtfulRascal8/rage-wars-recompiled/issues/new?template=bug_report.yml) · [Community comments](https://github.com/ArtfulRascal8/rage-wars-recompiled/discussions/1)

## Build identity

- Candidate executable SHA-256: `39B822892A9921EF5ADFB91C57601FA2D32B4B0A08C608FE45B918709289B432`
- Preserved baseline: `F67637BFB03202D7AC288564C232B098993623F76034CD68DBB1F935EEC38578`
- Windows product/file version: `0.1.0-beta.1`

## Evidence and limits

Release build, CLI startup, startup/profile contracts, seven package-safety tests, source-filter checks, 15 ROM-pinned controls/menu hooks, existing camera/control tests, and 50-cycle native pause/title menu fixtures passed. Production audit preserved all 3,206 callback definitions, with no CodeView debug symbols or forbidden capture probes. All 30 staged files passed the bounded personal-path/private-key/GitHub-token scan. This is not a comprehensive source or licensing audit.

Live UI verification was unavailable because the local computer-use runtime could not initialize. No live gameplay or headset acceptance is claimed for this candidate.

## Known issues before public release

- Live title/pause navigation, settings persistence, every Options entry point, menu/color parity, transitions, and save lifecycle need acceptance.
- Prior four-view audio skipping and fatal missing callback `0x00231F4C` remain unresolved.
- VR attachment, recentering, tracking-extension availability, fallback weapon coverage, near-wall behavior, barrel/aim-origin agreement, and actual moving shot impact/damage remain unaccepted.
- Calibrated models require private user-extracted inputs; automatic extraction is unavailable.
- Dependency attribution/licensing and later source privacy/provenance review remain open.

See [Known issues](https://github.com/ArtfulRascal8/rage-wars-recompiled/blob/main/KNOWN_ISSUES.md). The repository stays private and this release stays a draft until the owner chooses otherwise.
