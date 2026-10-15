# Setup — Beta 0.5.0 (`v0.5.0-beta.1`)

## Install or update

Use 64-bit Windows. Close the game and back up the existing whole profile. Extract the **complete** Windows ZIP into a fresh program folder. Keep `RageWarsRecompiled.exe`, `SDL2.dll`, `openxr_loader.dll`, and all included Microsoft runtime DLLs together. Launch the normal executable, select your own supported Turok: Rage Wars US v1.0 ROM and choose PC for desktop play. No development tools are needed to play.

Continue using the existing profile; do not replace it with packaged defaults. No populated profile or Controller Pak is bundled. Saves/settings default to `%LOCALAPPDATA%\XR64\RageWars`, with game data under `user-data`. The optional `XR64_PROFILE_DIR` environment variable selects a different **whole profile**. Moving the program does not reset it. Keep the old program folder and backup as a fallback, and update your shortcut. There is no automatic updater or save migration.

At the start screen press Escape. After it, arrows/WASD navigate menus and Enter/Space or left click confirms; Escape goes back. Modern mouse aim is the default for a fresh profile; existing preferences are preserved. Use Pause → Options → PC OPTIONS for display, controls, devices, players and desktop models; open VR OPTIONS for headset settings. Keyboard Pause defaults to Enter; mouse buttons fire/secondary-fire, Space jumps, Q/E or wheel cycle weapons, Tab opens the weapon wheel. Controller defaults: right trigger fires, A jumps, B secondary-fire, shoulders cycle weapons, Start pauses and X opens the weapon wheel.

## Multiplayer and player input

Choose **MULTIPLAYER** on the main menu and select the number of players. The game's own menu decides how many players take part; this release makes the multiplayer and split-screen code available and lets each player use a different input.

**Defaults.** Player 1 uses the combined keyboard, mouse and first gamepad (or VR touch controllers) exactly as in earlier betas. Players 2–4 use the next connected gamepads in the order they were connected, and are off when no gamepad is available.

**PLAYERS page.** Open **PC OPTIONS → PLAYERS** (during play: **Pause → Options → PC OPTIONS → PLAYERS**). Each **PLAYER** row cycles through **AUTO**, **KEYBOARD**, **GAMEPAD 1** to **GAMEPAD 4**, **VR** and **OFF**. **(NONE)** after a name means that device is not connected; **(TAKEN)** means a lower-numbered player already uses it. **ALL PLAYERS: AUTO** restores the defaults. Typical setups: player 1 **KEYBOARD** with player 2 **GAMEPAD 1**; several players each on a gamepad; one player on **VR** with the others on desktop devices. Gamepads are numbered in the order they were connected; you can plug one in while the game is running.

**Config file.** The same assignment is saved in `%LOCALAPPDATA%\XR64\RageWars\input-assignment-v1.ini` (override with `XR64_INPUT_ASSIGNMENT_CONFIG`). One line per player; `#` or `;` start a comment:

```
player1 = keyboard
player2 = gamepad1
player3 = auto
player4 = auto
```

Values are `auto`, `keyboard`, `gamepad1`–`gamepad4`, `vr` and `off`. `XR64_PLAYER1_INPUT` to `XR64_PLAYER4_INPUT` override the file. Mouse look, native movement and the VR camera follow the player who owns the keyboard and mouse (or the headset); with the defaults that is player 1. Only player 1 has a controller pak and save profile.

## How to play in VR

This Windows PC program uses OpenXR. Connect your headset to the PC and select its active OpenXR runtime before launching. Quest headsets can use PC connection software such as Virtual Desktop; the download is not a standalone Quest application.

1. Launch **RageWarsRecompiled.exe**, select your supported ROM, and choose **VR** in setup (or **PC** to start on desktop).
2. Open **Options → VR OPTIONS** (during play: **Pause → Options → VR OPTIONS**). Set **MOTION CONTROLS: ON**. VR mode selects calibrated weapon models automatically; supply your own `.rwpm`, `.placement`, `.muzzle`, and calibration files. These files are not bundled and extraction is not automatic. **PC OPTIONS → MODELS** controls desktop weapon models.
3. Choose **ENTER VR** to enter VR or **RETURN TO PC** to return to desktop. **VR STARTUP** controls the saved startup preference. Keep input selection on Auto for Touch unless assigning the headset explicitly on the PLAYERS page.
4. Face forward and use **RECENTER VIEW** or press **F12** on the PC keyboard. Set **MOVE DIRECTION** to **BODY**, **HEAD**, or **LEFT HAND** as preferred.
5. Default Touch gameplay controls: left stick moves, right stick turns, right trigger fires, A jumps, right grip or B uses secondary fire, X/Y cycle weapons, and Menu pauses. In menus, use the left stick, A to confirm, B to go back, and Menu for Start. Bindings are under **VR OPTIONS → MORE VR OPTIONS → VR CONTROLLER BINDINGS**; use **MORE** to page through the list.
6. Press **Escape** on the PC keyboard at the start screen. After it, arrows/WASD navigate menus, Enter/Space confirms, and Escape goes back.

If no headset is detected, check the connection and active OpenXR runtime, then retry ENTER VR. See [Known issues](KNOWN_ISSUES.md) for device and gameplay coverage.

## Feedback and files

Report version `v0.5.0-beta.1` and the executable SHA-256 with reproducible bugs. Do not upload ROMs, extracted assets, Controller Paks or unreviewed logs. See the release notes and known issues for evidence limits. Licenses/attributions are in COPYING.txt, MODIFICATIONS.txt, THIRD-PARTY-NOTICES.txt and licenses/.
