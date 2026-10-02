# Credits and notices

- **XR64 Studios** — Rage Wars Recompiled port integration, PC settings, OpenXR support and reconstruction/build tooling.
- [N64Recomp](https://github.com/N64Recomp/N64Recomp) and [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) — generator/runtime source with explicit local modifications; GNU GPLv3 distribution and upstream component notices.
- [SDL](https://github.com/libsdl-org/SDL), version 2.30.11 — platform, input and audio; official unchanged x64 DLL.
- [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK), version 1.1.58 — source-built loader at commit 472d817ffe066d5be09a351b0d39ff420141208b; Apache-2.0 or MIT, with JsonCpp and per-file attribution.
- [nlohmann JSON](https://github.com/nlohmann/json), version 3.12.0 — MIT, copyright 2013–2025 Niels Lohmann; complete notice included.
- [Ares](https://github.com/ares-emulator/ares) RSP vector implementation — ISC-style notice, copyright 2004–2021 ares team, Near et al; original embedded notice preserved.
- [odashi/encoding](https://github.com/odashi/encoding) EUC-JP conversion — MIT, copyright 2017 Yusuke Oda; complete notice included.
- [moodycamel concurrentqueue](https://github.com/cameron314/concurrentqueue) and semaphore adaptation — BSD/Zlib notices retained from all three compiled headers.
- Rabbitizer, fmt, xxHash and other vendored generator/runtime components — upstream license texts included and indexed in THIRD-PARTY-NOTICES.txt.
- Microsoft Visual C++ runtime — unmodified app-local x64 redistributable DLLs; redistribution notice included.
- [LibUltraShip Fast3D](https://github.com/Kenix3/libultraship/tree/main/src/fast) — field-extraction reference identified in the renderer alongside libultra GBI protocol macros. Fast3D MIT reference notice (2020 Emill, MaikelChan) is preserved. The historical reference revision was not recorded; the current reference notice does not establish that revision.
- [Alex LeTux's Perfect Dark VR](https://github.com/Alex-LeTux/perfect_dark_VR) — reference for OpenXR lifecycle/tracking-space separation and project-page presentation. Recorded code reference: 67ea20c86986c6bc85687f26a27418b266af309c; MIT reference notice, copyright 2022 Ryan Dwyer. The port's OpenXR source documents consultation without importing game-specific camera/input code.

The port/runtime integration, modifications and combined distribution use GNU
GPLv3; see [COPYING.txt](COPYING.txt) and dated [MODIFICATIONS.txt](MODIFICATIONS.txt).
Upstream permissive copyrights and licenses remain applicable and are preserved
in both downloads. Complete matching source and rebuild instructions accompany
the binary on the [release page](https://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.1.0-beta.1).

XR64 Studios records its decision to distribute a non-commercial fan
recompilation. No rights-holder permission or legal finding is claimed.
Original Turok: Rage Wars code/content belong to their respective owners.
No ROM file or extracted assets are supplied; translated guest code and
ROM-derived reconstruction edits are included as described in the notices.
