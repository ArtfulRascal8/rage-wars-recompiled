# Rage Wars Recompiled - Beta 0.2.1 (`v0.2.1-beta.1`)

Windows x64 desktop and experimental PC VR by XR64 Studios. **VR requires alternate weapon models and motion controls to be enabled.** See [SETUP.md](SETUP.md) for the complete VR guide.

## Focused crash repairs

- Repaired a narrow resource-decoder dispatch path associated with the reported crash as match results would open. The three callbacks already existed in the build; this caller now invokes those original-ROM bodies directly instead of relying on their availability in the active guest lookup map. Other indirect calls retain normal lookup.
- Included the earlier Save-path repair: nine original fixed calls now invoke five existing compiled guest bodies directly. This addresses the separately reported missing-callable path near saving.
- Preserved the 0.2.0 pickup clock correction, Syra and specifically reported Warclubs fixes, post-mission Quit, black splash transitions, normal controls/audio, VR instructions, and production crash reports.

The intermittent gameplay failure could not be reliably reproduced. Builds and targeted automated checks passed, but player confirmation is pending. This release does not claim that all intermittent crashes are fixed, and the underlying overlay/code-residency cause remains unproven.

## Installation and safe update

Close the game and back up the whole existing profile. Extract the **complete** Windows ZIP into a fresh program folder; keep the executable and every included DLL together. Update your shortcut and continue using the existing profile. Do not delete or replace your saves/settings. Keep the 0.2.0 program folder and profile backup as a fallback.

Default profile: `%LOCALAPPDATA%\XR64\RageWars`; optional whole-profile override: `XR64_PROFILE_DIR`. No profile, Controller Pak, save, ROM, or extracted model asset is bundled. There is no automatic updater or save migration. Launch **RageWarsRecompiled.exe**, select your own supported US v1.0 ROM, and choose PC or VR. Press Escape at the start screen.

## How to play in VR

**VR requires alternate weapon models and motion controls to be enabled.**

Connect the PC headset and make its OpenXR runtime active, then launch the normal executable and select VR in setup. This ZIP is a Windows PC VR program, not a standalone Quest application.

In **Options Ã¢â€ â€™ PC OPTIONS** (during play: **Pause Ã¢â€ â€™ Options Ã¢â€ â€™ PC OPTIONS**), select **MODELS: CALIBRATED** for the required alternate weapon models. Supply your own `.rwpm`, `.placement`, `.muzzle`, and calibration files; extraction is not automatic. Under **PC OPTIONS Ã¢â€ â€™ VR** enable **MOTION CONTROLS: ON**. Use **ENTER VR** from desktop and **RETURN TO PC** to leave VR. For Touch use Auto input selection.

Face forward and press F12 on the PC keyboard to recenter. Default Touch gameplay controls: left stick moves, right stick turns, right trigger fires, A jumps, right grip/B secondary-fire, X/Y cycle weapons and Menu pauses. In menus: left stick navigates, A confirms, B goes back, Menu sends Start. Press Escape on the PC keyboard at the start screen. Bindings are under **VR Ã¢â€ â€™ MORE VR OPTIONS Ã¢â€ â€™ VR CONTROLLER BINDINGS**. If detection fails, check the connection and active runtime and retry ENTER VR. Desktop play remains available without a headset. See SETUP.md for the full guide.

## Validation and limits

Final executable SHA-256: `A56AB4AF91A6CCECA5F9505DA474DD84E18A9A7A4ADD06F617F1EBAB0BA043DF`.

- The final matching source kit reconstructs all 60 guest/audio inputs from a user-supplied ROM, retaining 3,751 function definitions and 545 verified recovery records.
- The versioned Release build, nine release contracts, compiler-input closure audit, production filtering, package/notice checks, and exact ZIP/privacy validation passed.
- 48 native decoder RAM/register comparisons passed with synthetic streams and unavailable callable lookup; the earlier 16 Save-path fixture comparisons passed again. The final regenerated guest files exactly match their tested inputs. These checks do not simulate a complete match.
- A disposable-profile Setup startup and normal close were checked; no live match-end, save/close/relaunch, or headset acceptance is claimed for these final bytes.

Earlier owner gameplay/VR observations remain attributed to their earlier builds. Live results/Continue, persistence, broader campaign/maps, all previous-fix regressions, and Quest 3 through Virtual Desktop need player confirmation. Independent-machine/donor-denying OS validation remains unperformed. See KNOWN_ISSUES.md.

## Downloads and matching source

- `RageWarsRecompiled-v0.2.1-beta.1-windows-x64.zip`: complete portable program, DLLs, instructions, and licenses.
- `RageWarsRecompiled-v0.2.1-beta.1-source.zip`: matching public repository snapshot, including `source/` and BYO-ROM reconstruction instructions.
- `SHA256SUMS.txt`: SHA-256 for both ZIPs.

[Beta 0.2.0 and its original assets](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.2.0-beta.1) are retained as a fallback. Original rights remain with their respective holders. No ROM/proprietary asset download is provided.
