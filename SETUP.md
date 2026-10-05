# Setup — Beta 0.2.1 (`v0.2.1-beta.1`)

## Install or update

Use 64-bit Windows. Close the game and back up the existing whole profile. Extract the **complete** Windows ZIP into a fresh program folder. Keep `RageWarsRecompiled.exe`, `SDL2.dll`, `openxr_loader.dll`, and all included Microsoft runtime DLLs together. Launch the normal executable, select your own supported Turok: Rage Wars US v1.0 ROM and choose PC for desktop play. No development tools are needed to play.

Continue using the existing profile; do not replace it with packaged defaults. No populated profile or Controller Pak is bundled. Saves/settings default to `%LOCALAPPDATA%\XR64\RageWars`, with game data under `user-data`. The optional `XR64_PROFILE_DIR` environment variable selects a different **whole profile**. Moving the program does not reset it. Keep the old program folder and backup as a fallback, and update your shortcut. There is no automatic updater or save migration.

At the start screen press Escape. After it, arrows/WASD navigate menus and Enter/Space or left click confirms; Escape goes back. Modern mouse aim is the default for a fresh profile; existing preferences are preserved. Use Pause → Options → PC OPTIONS for display, controls, devices, models and VR. Keyboard Pause defaults to Enter; mouse buttons fire/secondary-fire, Space jumps, Q/E or wheel cycle weapons, Tab opens the weapon wheel. Controller defaults: right trigger fires, A jumps, B secondary-fire, shoulders cycle weapons, Start pauses and X opens the weapon wheel.

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

## Feedback and files

Report version `v0.2.1-beta.1` and the executable SHA-256 with reproducible bugs. Do not upload ROMs, extracted assets, Controller Paks or unreviewed logs. See the release notes and known issues for evidence limits. Licenses/attributions are in COPYING.txt, MODIFICATIONS.txt, THIRD-PARTY-NOTICES.txt and licenses/.
