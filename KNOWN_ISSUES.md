# Candidate limitations

These are unresolved items inherited from F67637BF plus the 39B82289 successor acceptance gates; not every session necessarily reproduces each issue.

- The title/lobby PC Options integration passed offline native lifecycle tests. Live title/pause coverage, rendering, navigation and persistence remain unverified.
- The owner observed improved Modern desktop mouse feel and resolution of camera/character/weapon separation and flipping in exercised play. Full desktop scenario coverage, movement cadence, and impact/damage accuracy remain unverified.
- VR has not received owner acceptance on this exact build. Head/hand attachment, recentering, near-wall behavior, and tracking-extension availability need headset evidence.
- VR shots use a separate current-time sample up to 50 ms old. Controller aim origin versus calibrated barrel origin and moving shot impact/damage remain unresolved. Native fallback weapon tracking and visibility have limitations.
- Earlier four-view audio skipping and a fatal missing callback (0x00231F4C) remain unresolved in the record. Interactive volume listening and physical device-removal recovery need acceptance.
- Full menu/color parity, PC/VR transitions, all-weapon muzzle coverage, menu-back behavior, and Controller Pak lifecycle need regression coverage.
- Calibrated weapon models and calibration files are private local inputs, not supplied assets. Automatic extraction is unavailable.
- Clean source-only reconstruction has not been established. Source and dependency licensing review remains a prerequisite to public binary distribution and later source publication.

No stable-release, complete-playthrough, or standalone Quest support claim is made.
