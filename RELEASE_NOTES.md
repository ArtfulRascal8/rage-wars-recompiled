# Rage Wars Recompiled — Beta 0.2.0 (`v0.2.0-beta.1`)

Windows x64 desktop and experimental PC VR by XR64 Studios. **VR requires alternate weapon models and motion controls to be enabled.** See [SETUP.md](SETUP.md) for the complete VR guide.

## Fixes

- Fixed a timer-wrap bug that could make rotating pickups disappear. Ammo and health pickup visibility was confirmed in two desktop matches on the earlier 0.2.0 clock-corrected candidate; this newly built executable has not received that gameplay check. Power-ups and all items/maps are not claimed verified.
- Carried forward the accepted fixes for Syra defeat, the specifically reported Warclubs path, and choosing Quit instead of Save or Continue after a mission. This is not an all-bodyguards fix.
- Carried forward black transitions between splash cards for both desktop and VR rendering.
- Preserved the accepted desktop controls/audio behavior. These were already included in the initial beta; they are not presented as new controls/audio repairs in this release.
- Removed candidate tracing/developer launch options from the consumer runtime and preserved F12 VR recentering. Normal settings, fullscreen, gameplay controls and bounded versioned error reports remain available.

The pickup correction returns the existing native timer owner's full monotonic counter through the guest 64-bit return ABI. The native VI adapter leaves the original VI manager dormant; using only 32-bit Count could create an invalid unsigned frame interval at wrap. The source recipe reproduces the corrected generated function from the user's ROM. Original item transforms and trigonometry are unchanged.

## Installation and safe update

Close the game and back up the whole existing profile. Extract the complete Windows ZIP into a fresh program folder; keep the executable and every required DLL together. Continue using the existing profile rather than replacing it with defaults. Update your shortcut and keep the old program folder plus backup as a fallback.

Default profile: `%LOCALAPPDATA%\XR64\RageWars`. Optional whole-profile override: `XR64_PROFILE_DIR`. Game data resides under `user-data`. No profile, Controller Pak, save, ROM or extracted model asset is bundled. There is no automatic updater or save migration. Launch **RageWarsRecompiled.exe**, select your own supported US v1.0 ROM and choose PC or VR. At the start screen press Escape; Enter and Space do not advance it.

## How to play in VR

**VR requires alternate weapon models and motion controls to be enabled.**

Connect the PC headset and make its OpenXR runtime active, then launch the normal executable and select VR in setup. This ZIP is a Windows PC VR program, not a standalone Quest application.

In **Options → PC OPTIONS** (during play: **Pause → Options → PC OPTIONS**), select **MODELS: CALIBRATED** for the required alternate weapon models. Supply your own `.rwpm`, `.placement`, `.muzzle`, and calibration files; extraction is not automatic. Under **PC OPTIONS → VR** enable **MOTION CONTROLS: ON**. Use **ENTER VR** from desktop and **RETURN TO PC** to leave VR. For Touch use Auto input selection.

Face forward and press F12 on the PC keyboard to recenter. Default Touch gameplay controls: left stick moves, right stick turns, right trigger fires, A jumps, right grip/B secondary-fire, X/Y cycle weapons and Menu pauses. In menus: left stick navigates, A confirms, B goes back, Menu sends Start. Press Escape on the PC keyboard at the start screen. Bindings are under **VR → MORE VR OPTIONS → VR CONTROLLER BINDINGS**. If detection fails, check the connection and active runtime and retry ENTER VR. Desktop play remains available without a headset. See SETUP.md for the full guide.

## Evidence and beta limits

**This exact release executable:** SHA-256 `A40C5E5403BFC0AE8EF5337791DA2C19FDED92F32DB5EBFFC2582CC9B3524C9C`. It differs from the earlier owner-tested candidate because consumer cleanup and recentering were rebuilt.

- The exact extracted Windows ZIP launched a responsive normal setup window and closed with exit code 0 using a fresh disposable profile and twice using a disposable older-profile copy. These are launcher checks, not an in-game save/upgrade round trip. The copied Pak remained byte-identical.
- All nine release contracts passed: clock wrap/o32 return, XR controls/camera, decoded frame ownership, audio output, weapon calibration, modern controls, package safety and CLI help.
- Existing settings write/read and legacy migration contracts passed. Existing reconstruction/dependency/package tests and the consumer-transformed synthetic startup/Pak contract passed; the latter uses a dialog stub.
- Source-built generators reproduced all 60 guest/audio inputs, including the corrected clock body. The final host was built from the matching source kit. Compiler-input closure, all 133 consumer-transformed sources, 3,751 guest definitions, GUI/no-CodeView checks, executable privacy filtering, required DLL/notice identities and ZIP contents passed.
- The public source, source ZIP and Windows ZIP were recursively checked: zero Controller Pak files, zero populated profile/user-data directories, zero save files and zero files copied from the owner's profile/backups. The four generic path-detector/test-string scan hits were reviewed; no actual personal path or credential was present. These technical checks are not legal clearance.

**Earlier owner acceptance:** preceding 0.2.0 candidates passed Syra, the reported Warclubs path, post-mission Quit, controls/audio, and black splash transitions on desktop and Quest 3 through Virtual Desktop. The clock-corrected candidate passed reported ammo/health visibility in two desktop matches. An earlier consumer candidate retained new saved progress after close/relaunch. Those outcomes are carried forward as earlier evidence, not relabeled as tests on this executable.

**Not tested on these final bytes:** live gameplay/physical controls/audio, ammo/health or power-up visibility, all items/maps, individual prior-fix regressions, in-game save/close/relaunch, a complete copied Beta 0.1 save upgrade round trip, VR startup/head/controller tracking and Quest 3 through Virtual Desktop. Interactive automation was unavailable in the release environment; no headset execution is claimed. Synthetic clock/audio/controls tests do not substitute for those checks.

Broader campaign/menu, eight dynamic destinations, worker PI-register support, VR weapon attachment/near-wall/shot accuracy and audio-device recovery coverage remain beta limitations. Independent-machine and donor-denying OS validation remain unperformed. No complete playthrough or blanket PCVR compatibility is claimed. See KNOWN_ISSUES.md.

## Downloads and matching source

- `RageWarsRecompiled-v0.2.0-beta.1-windows-x64.zip`: complete portable program, DLLs, instructions and licenses.
- `RageWarsRecompiled-v0.2.0-beta.1-source.zip`: the matching reviewed public repository snapshot, including `source/` and [BYO-ROM build instructions](source/docs/PC_RECONSTRUCTION.md).
- `SHA256SUMS.txt`: SHA-256 for both immutable ZIPs.

Required third-party notices and GPLv3/modification texts are included in both downloads. No ROM/proprietary asset download is provided; original rights remain with their respective holders. The original [Beta 0.1 release and assets](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.1.0-beta.1) are preserved.
