# Rage Wars Recompiled

**Turok: Rage Wars for Windows PC, with experimental OpenXR VR support.**
A non-commercial fan recompilation by XR64 Studios.

[Download v0.1.0-beta.1](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.1.0-beta.1) · [Setup](SETUP.md) · [Report a bug](https://github.com/ArtfulRascal8/rage-wars-recompiled/issues/new?template=bug_report.yml) · [Discussions](https://github.com/ArtfulRascal8/rage-wars-recompiled/discussions) · [Known issues](KNOWN_ISSUES.md)

## v0.1.0-beta.1

The first public beta uses tested build **47385556**. Owner testing confirmed
normal four-player preview and one-player/three-bot desktop combat rendering,
working controls and good audio. See [release notes](RELEASE_NOTES.md) for the
exact build identity and validation limits. General gameplay and PCVR testing
continue; independent-machine or donor-denying sandbox validation remains open.

## How to play

1. Extract the complete Windows x64 release ZIP.
2. Open **RageWarsRecompiled.exe**.
3. Select your own supported **Turok: Rage Wars US v1.0 ROM** in setup. The loader validates it.
4. Choose **PC** for desktop play. **VR** requires a separately installed active OpenXR runtime and is experimental.
5. At the game's start screen, press **Escape** to advance. **Enter and Space do not advance this screen** in this beta.

No ROM file, extracted assets, saves or weapon models are supplied. The binary
contains translated guest code and the source includes reconstruction edits
derived from the original program. See [Setup](SETUP.md) for controls and profiles.

## Settings

Use **Pause → Options → PC Options** for display, controls, input devices, models
and VR settings. The owner confirmed pause Options in earlier build 39B82289.
Title/lobby Options also exposes PC Options and passed native lifecycle tests;
complete live menu coverage and settings persistence remain unverified.

## Feedback

Use [Issues](https://github.com/ArtfulRascal8/rage-wars-recompiled/issues) for reproducible bugs and [the welcome discussion](https://github.com/ArtfulRascal8/rage-wars-recompiled/discussions/1)
for questions and feedback. The release has an **Announcements** discussion for
comments. Include version **v0.1.0-beta.1** and build **47385556** in reports.

## Source and credits

Download **RageWarsRecompiled-v0.1.0-beta.1-source.zip** from the [same release](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.1.0-beta.1).
It includes reviewed port/runtime/generator source, patches, configurations and
reconstruction instructions. Supply your own supported ROM to rebuild.
GitHub's automatic source archives contain only this documentation repository;
use the explicitly named source asset for the matching build source.

Licensing and dated changes: [COPYING.txt](COPYING.txt), [MODIFICATIONS.txt](MODIFICATIONS.txt).
Upstream copyrights and permissive notices are preserved in both downloads.
See [Credits](CREDITS.md) and [release naming](RELEASE_NAMING.md).

XR64 Studios chose to distribute this as a non-commercial fan recompilation.
This project is not affiliated with or endorsed by the original game's rights
holders. Original game code/content remain copyrighted by their respective
owners; no rights-holder permission or legal finding is claimed.
