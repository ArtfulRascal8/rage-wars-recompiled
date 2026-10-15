# Beta 0.5.0 limitations

- Multiplayer was confirmed by the owner with two players (keyboard and mouse, plus one gamepad). Three and four players, several gamepads at once, and VR as a player other than player 1 have not been demonstrated. Only player 1 has a controller pak and save profile.
- The in-game **PC OPTIONS → PLAYERS** page has not been confirmed by a player on this build; the same assignment can be made in `input-assignment-v1.ini`.
- Power-ups are not fully verified. The owner reported that Massive Damage appeared to apply the wrong amount of damage. No cause has been identified and no fix is claimed. Static comparison found the same original 11× multiplier and damage-helper code in earlier builds; the runtime cause remains unknown.
- Results/Continue, menu-back and save lifecycle were owner-accepted on the October 13 `7780328B` build. Those observations are retained; they are not a complete retest of every match transition on the final `7E5E22AB` build.
- Start screen: press Escape; Enter and Space do not advance it.
- Every item and map, complete campaign/menu coverage and all prior-fix regressions have not been demonstrated on the final executable.
- Quest 3 through Virtual Desktop and broader headset/runtime combinations have not been retested on this release executable. VR selects calibrated weapon models automatically; motion controls must be enabled and model/calibration files must be supplied by the user. No standalone Quest build is supplied.
- VR attachment, near-wall behavior, moving-shot impacts/damage and native fallback weapons remain experimental.
- The visual body delay that smooths character motion is a taste setting; its default is 80 ms and `XR64_LOCAL_BODY_DELAY_MS` changes it (0 turns it off). It delays what is drawn, not what the game computes.
- Fresh-profile persistence and copied earlier-beta profile upgrade coverage are limited to checks actually listed in the release notes. Back up your existing profile before updating.
- Some entries are reached only through function pointers or jump tables; entries in the US v1.1 reference listing still have uncertain placement in US v1.0. No complete playthrough is claimed.
- Interactive volume changes and physical audio-device removal/recovery need wider coverage.
- Independent-machine or donor-denying OS reconstruction remains untested; local manifest/build-input checks do not establish OS isolation.

See RELEASE_NOTES.md for exact executable evidence and earlier owner acceptance.

## Verification scope

Published executable: `7E5E22ABBEDFC8B2B3E1B8C5404B5CB2D8E78A1908FE6C0F158C61C779B1E0A2`. This is the selected TESTING executable, packaged without a game rebuild. Multiplayer input is owner-confirmed; the recorded two-player setup uses keyboard/mouse plus a gamepad. Earlier desktop/headset baseline acceptance remains tied to `7780328B`.

Fresh source reconstruction, package checks and the exact automated-check totals are recorded in [Release notes](RELEASE_NOTES.md). Synthetic/native helper fixtures do not establish gameplay acceptance. Experimental native leaf replacements are disabled by default. The idle-entry fixture link issue was repaired in the matching source kit and both fixture tests pass; no idle-entry gameplay pass is claimed.

The selected build passed 47/47 applicable contracts. Its stale count test executable and two private-report-dependent audits were excluded from that run. A clean source build passed 36/36 selected contracts, then its newly built function-count and input-assignment checks passed 2/2. Six reconstruction helper tests and ten packaging helper tests also passed. These are automated build/fixture checks, not a complete match or headset test. The clean-built executable has a different hash (`CD1723B31C162BD388479F92363B900884A1B92B48CE6573AC778008D916DC4B`); it is build evidence and is not the published executable. The matching source reproduces the published build's raw guest/audio inputs exactly. The independent-machine limit above remains open.
