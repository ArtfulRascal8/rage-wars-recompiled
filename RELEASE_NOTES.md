# Rage Wars Recompiled — Beta 0.5.0 (`v0.5.0-beta.1`)

Windows x64 desktop and experimental PC VR by XR64 Studios.

## What's new

- Local multiplayer input assignment: keyboard/mouse, gamepads and VR can be assigned to individual player ports. Keyboard/mouse plus a gamepad has been owner-confirmed in multiplayer.
- Expanded original-ROM coverage: 756 additional recovered functions, 19 generator helpers and 3 cache shims bring the guest definition count from 3,759 to 4,537; 53 switch tables accompany the recovery.
- Native first-person view and movement, animation blending, higher-refresh presentation, captured-model filtering, broader material colour support and corrected HUD text.
- Revised VR HUD, laser, weapon tracking, locomotion and turning, plus separate PC/VR settings and expanded controller bindings.
- Accepted presentation features are enabled by default, including the 80 ms body presentation delay. Existing preferences remain in the user's profile.

## Install or update

Close the game and back up your whole profile. Extract the **complete Windows ZIP** into a fresh program folder; keep every included DLL with **RageWarsRecompiled.exe**. Select your own supported US v1.0 ROM and choose PC or VR. Press **Escape** at the start screen.

Profiles remain at `%LOCALAPPDATA%\XR64\RageWars`, or your `XR64_PROFILE_DIR` override. No populated profile, Controller Pak, ROM or extracted model asset is bundled. Keep your previous program folder and profile backup. See [Setup](SETUP.md) for multiplayer and current VR-menu instructions.

## Downloads and matching source

- `RageWarsRecompiled-v0.5.0-beta.1-windows-x64.zip`: complete portable program, DLLs, setup and notices.
- `RageWarsRecompiled-v0.5.0-beta.1-source.zip`: matching public snapshot, including `source/` and BYO-ROM reconstruction instructions.
- `SHA256SUMS.txt`: SHA-256 hashes of both ZIPs.

Executable SHA-256: `7E5E22ABBEDFC8B2B3E1B8C5404B5CB2D8E78A1908FE6C0F158C61C779B1E0A2`. It is the exact selected TESTING program; packaging changes its filename and documentation, not its executable bytes.

Fresh source-built generators reproduced all **60 pinned guest/audio inputs** and **4,537 guest definitions**. The reconstructed kit built successfully and passed **38 selected build/fixture checks**. The selected build passed **47 applicable contracts**, production filtering, compiler-input closure, and package/privacy checks. These results and their scope are detailed on the linked Known Issues page.

[Known issues and verification limits](KNOWN_ISSUES.md) contains unresolved damage behavior, untested scenarios and device coverage. Use the [issue tracker](https://github.com/ArtfulRascal8/rage-wars-recompiled/issues) for reproducible reports.

[Beta 0.2.1](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.2.1-beta.1), [Beta 0.2.0](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.2.0-beta.1) and [Beta 0.1.0](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.1.0-beta.1) remain available. Original copyrights and licenses are preserved in both downloads. No ROM/proprietary asset download is provided.
