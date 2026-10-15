# Release naming

| Item | Convention |
| --- | --- |
| Project | Rage Wars Recompiled |
| Studio | XR64 Studios |
| Repository | ArtfulRascal8/rage-wars-recompiled |
| Windows executable | RageWarsRecompiled.exe |
| Release tag | v0.5.0-beta.1 |
| Release title | Rage Wars Recompiled - v0.5.0-beta.1 |
| Download | RageWarsRecompiled-v0.5.0-beta.1-windows-x64.zip |
| ZIP root directory | RageWarsRecompiled |
| Matching source | RageWarsRecompiled-v0.5.0-beta.1-source.zip |
| Source ZIP root | RageWarsRecompiled-Source |
| Checksums | SHA256SUMS.txt |
| Build identity | Full executable SHA-256; eight-character prefix for display |
| Existing save namespace | %LOCALAPPDATA%\XR64\RageWars |

Windows x64 covers PC and experimental PCVR in one executable. Dependency DLL filenames remain upstream names. Any future standalone build needs its own platform label.

7E5E22AB identifies executable SHA-256 7E5E22ABBEDFC8B2B3E1B8C5404B5CB2D8E78A1908FE6C0F158C61C779B1E0A2, not a Git commit. A filename-only repack retains that hash. Any compiled menu or branding change creates a new build identity and must be described as a successor.
