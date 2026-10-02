# Beta limitations

These limits apply to v0.1.0-beta.1, build 47385556. Owner acceptance covers
four-player preview and one-player/three-bot desktop combat, including rendering,
controls and audio. It does not establish a complete playthrough or PCVR acceptance.

- **Start screen: press Escape. Enter and Space do not advance it.**
- Pause Options was owner-confirmed on earlier build 39B82289. Title/lobby PC Options passed native lifecycle tests; complete live menu coverage, every entry point and settings persistence remain unverified.
- The inherited fatal missing callback **0x00231F4C** remains open outside the exercised scenarios.
- Broader gameplay, movement cadence, impact/damage accuracy, all-weapon muzzle coverage, menu/color parity, PC/VR transitions, menu-back behavior and Controller Pak lifecycle need regression coverage.
- Interactive volume changes and physical audio-device removal/recovery need acceptance. Earlier preview crackling was repaired; the owner reported good preview/combat audio on this build.
- PCVR head/hand attachment, recentering, near-wall behavior and tracking-extension availability need headset evidence on this exact build.
- VR shots use a separate current-time sample up to 50 ms old. Calibrated barrel versus controller aim origin, moving-shot impacts/damage and native fallback weapon tracking/visibility remain unaccepted.
- Calibrated weapon models and calibration files require your own extracted inputs. Automatic extraction is unavailable.
- Independent Windows/VM or donor-denying sandbox reconstruction remains open. Local empty-output reconstruction and declared/read input auditing passed; this does not establish OS isolation.

There is no standalone Quest APK, stable-release or complete-playthrough claim.
