# Build Rage Wars Recompiled - v0.5.0-beta.1

Use the complete `source/` directory from the matching public snapshot or named source ZIP. It contains an explicit SHA-256 manifest and recipes; it excludes ROMs, generated game/audio source, assets, saves and build products.

## Requirements

- Windows x64, Visual Studio 2022 with Desktop development with C++, MSVC v143 **14.43.34808** (compiler **19.43.34809.0**), and Windows SDK **10.0.22621.0**.
- Python 3.11 or later, CMake, and your own supported Turok: Rage Wars US v1.0 `.z64` ROM.
- An empty work directory outside `source/`. Use a short path because MSVC FileTracker has path-length limits.

From the source directory, run in PowerShell (replace the example ROM and CMake paths):

```powershell
python scripts/reconstruct_pc_candidate.py --source-candidate . --rom "D:/Games/owner-supplied-rage-wars.z64" --work "D:/rw050/run" --cmake "C:/Program Files/CMake/bin/cmake.exe"
```

Use `--generate-only` to stop after the source-built generators and all exact guest/audio input checks. Without it, the script also builds the Windows player, runs its selected contracts, prepares the complete package, and audits dependencies and privacy. Public SDL2/OpenXR archives are hash-pinned; no donor game folder is used. `--offline` requires those archives already in the new work directory's dependency cache.

## Reconstruction gates

The recipe validates the whole ROM hash, 60 reviewed ROM table pins, 545 recovered closure records, the three saved closure generations and 3,751 canonical function hashes before applying changes. Existing layout/hooks are restored, then the bounded October 6 recovery layer is replayed, followed by the two-stage whole-ROM layer. The inner layer's admitted symbols are restored before it runs; no baseline pins are relaxed. The final 4,537 guest definitions and all 60 guest/audio files must match the reviewed raw-byte inventory exactly. The optional historical RAM snapshot is not used.

The default consumer build enables the accepted presentation features and keeps experimental native player leaf replacements off. Build switches exist for developers to opt in; those are not gameplay acceptance. Production mode disables private tracing, capture and replay facilities.

Outputs and logs remain in the supplied work directory. The program package is under `project/build/reconstructed-pc/package-stage`, with `RageWarsRecompiled.exe`, required DLLs, instructions and notices. Keep the whole package together. Game data stays in the existing profile location documented in SETUP.md.

The published executable is the owner-selected TESTING binary, SHA-256 `7E5E22ABBEDFC8B2B3E1B8C5404B5CB2D8E78A1908FE6C0F158C61C779B1E0A2`. The final source kit reproduces its pinned generated inputs. Native PE hashes may differ across clean builds; building/installing a program does not establish gameplay or headset acceptance. Independent-machine/donor-denying OS validation is separate from the local input audit. See the release's Known Issues page for coverage limits.
