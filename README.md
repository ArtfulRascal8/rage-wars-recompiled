# Rage Wars Recompiled

**Turok: Rage Wars for Windows PC, with experimental OpenXR VR support.** A non-commercial fan recompilation by XR64 Studios.

[Download Beta 0.2.1](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.2.1-beta.1) · [Setup](SETUP.md) · [Release notes](RELEASE_NOTES.md) · [Known issues](KNOWN_ISSUES.md) · [Report a bug](https://github.com/ArtfulRascal8/rage-wars-recompiled/issues)

**VR requires alternate weapon models and motion controls to be enabled.** Select **MODELS: CALIBRATED** under PC OPTIONS and **MOTION CONTROLS: ON** under VR. Required user-supplied model/calibration files are not bundled.

## Beta 0.2.1 (`v0.2.1-beta.1`)

Repairs a narrow callback-dispatch path associated with the reported match-end/results crash and includes the earlier Save-path call repairs. Builds and targeted automated checks passed. The intermittent gameplay failure could not be reliably reproduced; player confirmation is pending. Prior pickup, Syra, reported Warclubs, post-mission Quit, and splash fixes are retained. See RELEASE_NOTES.md for scope and verification limits.

## How to play

Extract the complete Windows x64 ZIP, keep its DLLs beside RageWarsRecompiled.exe and launch that executable. Select your own supported US v1.0 ROM and choose PC for desktop play. Press Escape at the start screen; Enter and Space do not advance it. Use Pause → Options → PC OPTIONS for settings.

To update: close the game, back up `%LOCALAPPDATA%\XR64\RageWars` (or the whole `XR64_PROFILE_DIR` profile), extract into a fresh program folder and continue using the existing profile. No saves or defaults are bundled. Keep the old program folder and backup.

## How to play in VR

**VR requires alternate weapon models and motion controls to be enabled.**

This is Windows PC VR through OpenXR, not a standalone Quest application. Install your headset/PC connection software, connect the headset, and make its OpenXR runtime active before launching. For Quest through Virtual Desktop, connect to the PC and select the intended PC OpenXR runtime in that software. Quest 3 through Virtual Desktop has not been retested on this release executable.

1. Launch **RageWarsRecompiled.exe**, select your supported ROM, and choose **VR** in setup. Choose **PC** for normal desktop play without a headset.
2. Open **Options → PC OPTIONS** (during play: **Pause → Options → PC OPTIONS**). Select the required alternate weapon models by changing **MODELS: ORIGINAL** to **MODELS: CALIBRATED**. Calibrated models require your own `.rwpm`, `.placement`, `.muzzle`, and weapon calibration files; these are not bundled or automatically extracted.
3. Open **PC OPTIONS → VR** and enable **MOTION CONTROLS: ON**. Both this and **MODELS: CALIBRATED** are required for the supported VR setup.
4. From desktop mode use **PC OPTIONS → VR → ENTER VR**. **VR STARTUP** controls the saved startup preference. Choose **Auto** for input device selection with Touch controllers; explicitly selecting Keyboard/Mouse can take precedence.
5. Face forward and press **F12** on the PC keyboard to recenter. Default Touch gameplay bindings: left stick moves, right stick turns, right trigger fires, A jumps, right grip or B uses secondary fire, X/Y select previous/next weapon, and Menu pauses. Menus use the left stick, A to confirm, B to go back and Menu for Start. Customize bindings under **VR → MORE VR OPTIONS → VR CONTROLLER BINDINGS**.
6. At the game's start screen use **Escape** on the PC keyboard to advance. Enter and Space do not advance that screen. After it, arrows/WASD navigate menus, Enter/Space confirms, and Escape goes back.
7. Use **PC OPTIONS → VR → RETURN TO PC** to return to desktop. If no headset is detected, check the connection and active OpenXR runtime, then retry **ENTER VR**. Desktop mode remains available.

Head/controller tracking, calibrated weapon attachment, near-wall behavior and shot impacts remain experimental. A calibration ray alone does not establish projectile accuracy.

## Source and credits

The reviewed build/reconstruction kit is in [source/](source/README.md). The matching named source ZIP contains this public snapshot; GitHub's automatic source archives also include the kit. Supply your own supported ROM and follow [PC reconstruction](source/docs/PC_RECONSTRUCTION.md). No ROM, extracted assets, saves or original-game test fixtures are supplied.

See [COPYING.txt](COPYING.txt), [MODIFICATIONS.txt](MODIFICATIONS.txt), [third-party notices](source/THIRD-PARTY-NOTICES.txt) and [Credits](CREDITS.md). Original game code/content remain copyrighted by their respective rights holders. XR64 Studios' non-commercial fan-distribution decision does not claim rights-holder permission or legal clearance. The binary contains translated guest code and the reconstruction recipe contains edits derived from the original program.

[Beta 0.2.0 and its original downloads](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.2.0-beta.1) remain available as a fallback. Beta 0.1 is also retained.
