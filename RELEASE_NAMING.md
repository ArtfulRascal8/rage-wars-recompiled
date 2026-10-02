# Release naming

| Item | Convention |
| --- | --- |
| Project | Rage Wars Recompiled |
| Studio | XR64 Studios |
| Repository | ArtfulRascal8/rage-wars-recompiled |
| Windows executable | RageWarsRecompiled.exe |
| Initial draft tag | v0.1.0-beta.1 |
| Release title | Rage Wars Recompiled - v0.1.0-beta.1 |
| Download | RageWarsRecompiled-v0.1.0-beta.1-windows-x64.zip |
| ZIP root directory | RageWarsRecompiled |
| Checksums | SHA256SUMS.txt |
| Build identity | Full executable SHA-256; eight-character prefix for display |
| Existing save namespace | %LOCALAPPDATA%\XR64\RageWars |

Windows x64 covers PC and experimental PCVR in one executable. Dependency DLL filenames remain upstream names. Any future standalone build needs its own platform label.

F67637BF identifies executable SHA-256 F67637BFB03202D7AC288564C232B098993623F76034CD68DBB1F935EEC38578, not a Git commit. A filename-only repack retains that hash. Any compiled menu or branding change creates a new build identity and must be described as a successor.
