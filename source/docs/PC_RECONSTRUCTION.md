# Rage Wars PC source reconstruction — v0.2.0-beta.1

XR64 Studios uses this workflow to recreate the reviewed v0.2.0-beta.1 guest and audio inputs from a manifested source kit and the separately supplied US v1.0 ROM. The generated executable and its complete Windows ZIP must come from this clean reconstruction before that ZIP is treated as the player release. Version 0.1.0-beta.1 remains the public fallback.

The source archive has an explicit SHA256 manifest. It excludes Git history, generated guest/audio source, ROMs, maps, profiles, saves, captures, logs, dependency build products and the previous install. The ROM is never copied into the source or package directory. The package retains the project’s existing copyright and permission notices; release preparation does not grant rights to original game material.

Extract the matching source ZIP, then change directory to its `source/` folder (the manifested kit). Run from Windows x64 with Visual Studio 2022, MSVC 14.43.34808 / compiler 19.43.34809.0, Windows SDK 10.0.22621.0, Python 3.11 or later and CMake. Use a new, empty work directory outside the extracted source. Keep it short so MSVC FileTracker can complete the OpenXR compiler check. For example:

```powershell
python scripts/reconstruct_pc_candidate.py `
  --source-candidate . `
  --rom <owner-supplied-US-v1.0-ROM.z64> `
  --work C:/rw-v020/run1 `
  --cmake <cmake.exe>
```

Add `--offline` only when the new project’s public dependency cache is already populated. Dependency sourcing uses the pinned public archives and notices; it does not consult an installed donor SDK or sibling source tree. The normal bootstrap sets `XR64_PUBLIC_RELEASE=ON` and turns call journals, replay diagnostics and frame capture off.

The recipe validates the whole ROM identity, the original 29 ROM table pins and the 31 reviewed table additions. It checks ROM bytes for 545 recovered function records, verifies their symbol and canonical-code identities, and replays the saved 496-, 46- and 3-function closure generations with the source-built N64Recomp tool. It then generates all 3,751 canonical guest functions and checks every function digest, including the 3,206 pins preserved from the prior recipe. Explicit reviewed edits reconstruct the accepted layout, registration metadata and the original 15 controls hooks. The 18 extra recovered-body edits are kept as exact call-target spelling patches; the evidence records them without asserting behavioral equivalence. Audio is regenerated from the pinned ROM regions and handler targets. The optional historical RAM snapshot is never opened.

The bootstrap compares every reconstructed guest file and the audio C++ file against the reviewed raw-byte manifest. It then configures the consumer build in public-release mode, runs the existing nine CTest contracts, the build-input audit, production audit, package checks and privacy audit. The packaging target produces the complete `project/build/reconstructed-pc/package-stage` folder, with all required DLLs and component notices. It is built in a fresh program folder so an existing install can remain available as a fallback; player saves and settings remain under `%LOCALAPPDATA%\XR64\RageWars` unless `XR64_PROFILE_DIR` selects a separate profile.

Run `--generate-only` to stop after source-built generator and exact guest/audio comparison. That mode does not build or validate the player ZIP. Full reconstruction produces `generator-build-provenance.json`, `closure-generator-replay.json`, `reconstruction-result.json`, phase logs, the reconstructed guest/audio inventory, the consumer package, dependency-input audit and privacy report under the work directory.

## Validation and limits

The bootstrap regenerates and compares all 60 guest/audio inputs, including the full 64-bit osGetTime edit, before compiling. The synthetic clock contract covers counter wrap and the o32 v0:v1 result. The public build disables private diagnostics and applies consumer filtering. Source-built generators and pinned public dependency archives are used; generated original-game files and a donor checkout are not required inputs.

Build contracts do not establish gameplay or headset acceptance. See release notes for final-package checks and earlier owner acceptance. Independent-machine and OS donor-denying validation remain untested. Tracked compiler reads are bounded evidence, not an OS isolation barrier.

## Create the Windows ZIP

After the full bootstrap succeeds, package its complete staged program folder. For the example work directory above:

```powershell
Copy-Item -LiteralPath C:/rw-v020/run1/project/build/reconstructed-pc/package-stage -Destination C:/rw-v020/RageWarsRecompiled -Recurse
Compress-Archive -LiteralPath C:/rw-v020/RageWarsRecompiled -DestinationPath C:/rw-v020/RageWarsRecompiled-v0.2.0-beta.1-windows-x64.zip
Get-FileHash -LiteralPath C:/rw-v020/RageWarsRecompiled-v0.2.0-beta.1-windows-x64.zip -Algorithm SHA256
```

Use a fresh destination and distribute the entire program folder. Local builds can have different PE/archive bytes due to compiler paths, timestamps and packaging metadata; the ROM-derived guest/audio inputs must match their pinned hashes exactly. No byte-identical independent-machine executable claim is made.
