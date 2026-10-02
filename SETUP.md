# Setup

## Windows PC

Use 64-bit Windows and extract every file from the release ZIP. Keep SDL2.dll, openxr_loader.dll, and the included Microsoft runtime DLLs beside RageWarsRecompiled.exe.

Launch RageWarsRecompiled.exe, select your own supported Turok: Rage Wars US v1.0 ROM, choose PC, and start the game. The loader appears on ordinary launches and accepts supported ROM byte orders through its file picker. No ROM download is provided.

**Start-screen exception (v0.1.0-beta.1): press Escape to advance past the game's start screen. Enter and Space do not advance that screen.** This behavior was reported during owner testing of candidate 39B82289.

After the start screen, menu navigation uses arrow keys or WASD, controller sticks or D-pad, and Enter/Space or left click to confirm. Escape or controller Back/B normally returns to the previous menu. Modern mouse aim is the default for a new profile; existing preferences are preserved.

## PC Options

Use Pause → Options → PC Options. The owner confirmed the pause Options submenu works on candidate 39B82289. Display settings include resolution, display mode, aspect ratio, FOV, and master volume. Controls and Input Devices provide sensitivity, inversion, profile/device selection, and bindings. Models selects Original or Calibrated.

The title/lobby Options path passed captured native lifecycle tests. Live title/lobby rendering and navigation, settings persistence, and complete Options-menu coverage remain pending.

## VR (experimental)

Install the headset vendor's OpenXR runtime separately, make it active, and choose VR in the loader. The same Windows executable supplies PC and PCVR modes. There is no standalone Quest APK in this release preparation.

For Touch input, choose Auto in Input Devices; an explicitly selected Keyboard/Mouse device can take precedence. VR settings expose motion controls and controller bindings. Runtime support for the current-time tracking conversion extension still needs verification on the target headset.

Calibrated weapon models require your own extracted .rwpm, .placement, .muzzle, and calibration files. Automatic ROM extraction is not implemented. These assets are not supplied. Missing models/anchors and native model fallback have known limitations. A calibration-room aiming ray does not verify actual projectile impacts or damage. Read [Known issues](KNOWN_ISSUES.md) before VR testing.

## Saves and settings

The existing profile location remains %LOCALAPPDATA%\XR64\RageWars, with game data under user-data. Renaming the executable does not migrate or reset this profile. Back up saves before testing a prerelease.

The optional XR64_PROFILE_DIR environment variable selects a separate local test profile. Do not attach ROMs, extracted assets, Controller Paks, or unreviewed full logs to public reports. Redact account names and machine paths from diagnostic excerpts.
