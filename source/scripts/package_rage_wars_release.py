#!/usr/bin/env python3
"""Stage the portable Rage Wars consumer using a strict, explicit file allowlist."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

MANIFEST = "package-manifest.json"
EXECUTABLE = "RageWarsRecompiled.exe"
BINARY_FILES = (
    EXECUTABLE,
    "SDL2.dll",
    "openxr_loader.dll",
    "MSVCP140.dll",
    "VCRUNTIME140.dll",
    "VCRUNTIME140_1.dll",
)
TOP_LEVEL_FILES = set(BINARY_FILES) | {"README.txt", "THIRD-PARTY-NOTICES.txt", "dependency-provenance.json", "COPYING.txt", "MODIFICATIONS.txt"}
LICENSE_LIMIT = 200_000


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def outside_repo(path: Path, repo_root: Path) -> bool:
    path = path.resolve()
    repo_root = repo_root.resolve()
    try:
        path.relative_to(repo_root)
    except ValueError:
        return True
    return False


def validate_x64_pe(path: Path) -> None:
    data = path.read_bytes()[:4096]
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"{path.name} is not a PE file")
    pe_offset = int.from_bytes(data[0x3C:0x40], "little")
    if pe_offset + 6 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{path.name} has no valid PE header")
    machine = int.from_bytes(data[pe_offset + 4:pe_offset + 6], "little")
    if machine != 0x8664:
        raise ValueError(f"{path.name} must be Windows x64 (PE machine 0x8664)")


def dependency_licenses(repo_root: Path) -> list[tuple[str, Path]]:
    found: list[tuple[str, Path]] = []
    for base in (repo_root / "toolchain/N64ModernRuntime", repo_root / "toolchain/N64Recomp"):
        if not base.is_dir():
            raise FileNotFoundError(f"Dependency source tree is missing: {base}")
        for path in sorted(base.rglob("*")):
            if not path.is_file() or ".git" in path.parts or path.stat().st_size > LICENSE_LIMIT:
                continue
            if path.name.lower().startswith(("license", "copying")):
                found.append((path.relative_to(repo_root).as_posix(), path))
    if not found:
        raise FileNotFoundError("No dependency license texts were found")
    return found


def sdl_license(sdl_include: Path) -> str:
    header = sdl_include / "SDL.h"
    if not header.is_file():
        raise FileNotFoundError(f"SDL2 header not found: {header}")
    match = re.search(r"/\*.*?\*/", header.read_text(encoding="utf-8", errors="replace"), re.S)
    if not match:
        raise ValueError(f"SDL2 license comment was not found in {header}")
    return match.group(0).strip() + "\n"


def dependency_metadata(repo_root: Path, root: Path, runtime_dir: Path, sdl_include: Path) -> dict:
    root = root.resolve()
    data = json.loads((root / "dependencies.json").read_text(encoding="utf-8"))
    if data.get("schema") != "xr64.pc-dependencies.v1":
        raise ValueError("Unsupported dependency manifest")
    lock_path = repo_root / "scripts/pc_dependencies.lock.json"
    if sha256(lock_path).lower() != data["lock_sha256"]:
        raise ValueError("Dependency lock identity mismatch")
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    if data["sdl2"] != lock["sdl2"] or data["openxr"] != lock["openxr"]:
        raise ValueError("Dependency provenance differs from the lock")
    required_notices = {"SDL2.txt", "OpenXR-MIT.txt", "OpenXR-Apache-2.0.txt", "OpenXR-JsonCpp.txt", "OpenXR-ATTRIBUTION.txt"}
    if set(data["notices"]) != required_notices:
        raise ValueError("Dependency notice set is incomplete")
    if not {"sdl_dll", "xr_dll"} <= data["artifacts"].keys():
        raise ValueError("Dependency artifact set is incomplete")
    for record in list(data["artifacts"].values()) + list(data["notices"].values()):
        path = (root / record["path"]).resolve()
        if not path.is_relative_to(root) or not path.is_file() or sha256(path).lower() != record["sha256"]:
            raise ValueError("Dependency artifact/notice identity mismatch")
    if sdl_include.resolve() != (root / data["paths"]["sdl_include"]).resolve():
        raise ValueError("SDL headers must come from the pinned dependency cache")
    for key, filename in (("sdl_dll", "SDL2.dll"), ("xr_dll", "openxr_loader.dll")):
        if sha256(runtime_dir / filename).lower() != data["artifacts"][key]["sha256"]:
            raise ValueError("Packaged DLL differs from the dependency manifest: " + filename)
    if data["artifacts"]["sdl_dll"]["sha256"] != lock["sdl2"]["dll_sha256"]:
        raise ValueError("SDL2 DLL differs from the version pin")
    return data


def concurrentqueue_notices(repo_root: Path) -> tuple[str, dict]:
    base = repo_root / "toolchain/N64ModernRuntime/thirdparty/concurrentqueue"
    headers = ("concurrentqueue.h", "blockingconcurrentqueue.h", "lightweightsemaphore.h")
    text = ["moodycamel concurrentqueue: BSD-2-Clause option; semaphore adaptation: Zlib.",
            "These notices are retained from the vendored headers used by the x64 runtime.", ""]
    hashes = {}
    for header in headers:
        path = base / header
        source = path.read_text(encoding="utf-8")
        hashes[header] = sha256(path)
        leading = source.split("#pragma once", 1)[0]
        if header == "lightweightsemaphore.h":
            start = source.index("// LICENSE:")
            end = source.index("#if defined(_WIN32)", start)
            leading += source[start:end]
        text.extend([header, leading, ""])
    return "\n".join(text), hashes


def package_inventory(package: Path) -> set[str]:
    inventory: set[str] = set()
    for path in package.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Package contains a symbolic link: {path}")
        if path.is_file():
            inventory.add(path.relative_to(package).as_posix())
    return inventory


def audit_package_content(package: Path) -> set[str]:
    inventory = package_inventory(package)
    for relative in inventory:
        if relative == MANIFEST:
            continue
        if relative not in TOP_LEVEL_FILES and not (relative.startswith("licenses/") and len(relative.split("/")) == 2):
            raise ValueError(f"Package path is outside the file allowlist: {relative}")
        suffix = Path(relative).suffix.lower()
        if suffix in {".z64", ".v64", ".n64", ".pak", ".pdb", ".map", ".log", ".ini", ".cfg", ".toml", ".bin"}:
            raise ValueError(f"Private game/save/config/build file is not allowed: {relative}")
        if relative.startswith("licenses/"):
            content = (package / relative).read_bytes()
            if b"\x80\x37\x12\x40" in content or b"RAGE_WARS_PRIVATE_GAME_DATA" in content:
                raise ValueError(f"License payload contains a ROM/game-data marker: {relative}")
    return inventory

def verify_owned_package(package: Path) -> None:
    manifest_path = package / MANIFEST
    if not package.is_dir() or package.is_symlink() or not manifest_path.is_file():
        raise FileExistsError(f"Refusing to replace an unowned package directory: {package}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"Existing package manifest is unreadable: {manifest_path}") from exc
    files = manifest.get("files")
    if manifest.get("schema") != "xr64.rage-wars.package.v1" or not isinstance(files, dict):
        raise ValueError(f"Existing package has an unsupported manifest: {manifest_path}")
    if any(not (relative in TOP_LEVEL_FILES or (relative.startswith("licenses/") and len(relative.split("/")) == 2)) for relative in files):
        raise ValueError("Existing package manifest contains a path outside the package allowlist")
    actual = audit_package_content(package)
    if actual != set(files) | {MANIFEST}:
        raise ValueError("Existing package has untracked or missing files; refusing to replace it")
    for relative, expected_hash in files.items():
        source = package / relative
        if not source.is_file() or sha256(source) != expected_hash:
            raise ValueError(f"Existing package file differs from its manifest: {relative}")


def build_package(
    repo_root: Path, executable: Path, runtime_dir: Path, sdl_include: Path, output: Path,
    *, dependency_root: Path, staging: bool = False
) -> dict[str, object]:
    repo_root = repo_root.resolve()
    executable = executable.resolve()
    runtime_dir = runtime_dir.resolve()
    sdl_include = sdl_include.resolve()
    output = Path(os.path.abspath(output))

    if not outside_repo(output, repo_root) and not (staging and output.resolve().is_relative_to(repo_root / "build")):
        raise ValueError("Package output must be outside the repository")
    if executable.name != EXECUTABLE:
        raise ValueError(f"Release executable must be named {EXECUTABLE}")
    inputs = {
        EXECUTABLE: executable,
        "SDL2.dll": runtime_dir / "SDL2.dll",
        "openxr_loader.dll": runtime_dir / "openxr_loader.dll",
        "MSVCP140.dll": runtime_dir / "MSVCP140.dll",
        "VCRUNTIME140.dll": runtime_dir / "VCRUNTIME140.dll",
        "VCRUNTIME140_1.dll": runtime_dir / "VCRUNTIME140_1.dll",
    }
    for name, source in inputs.items():
        if not source.is_file():
            raise FileNotFoundError(f"Required release dependency is missing: {source}")
        validate_x64_pe(source)

    required_notice_inputs = (
        "COPYING.txt", "MODIFICATIONS.txt",
        "toolchain/N64ModernRuntime/thirdparty/json/LICENSE.MIT",
        "toolchain/N64ModernRuntime/librecomp/include/librecomp/LICENSE-Ares-RSP.txt",
        "toolchain/N64ModernRuntime/librecomp/src/LICENSE-odashi-encoding.txt",
        "toolchain/N64ModernRuntime/LICENSE-Fast3D-reference.txt",
    )
    for relative in required_notice_inputs:
        if not (repo_root / relative).is_file():
            raise FileNotFoundError("Required release notice is missing: " + relative)

    dependency_root = dependency_root.resolve()
    dependencies = dependency_metadata(repo_root, dependency_root, runtime_dir, sdl_include)
    queue_text, queue_hashes = concurrentqueue_notices(repo_root)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        verify_owned_package(output)

    with tempfile.TemporaryDirectory(prefix=f".{output.name}-stage-", dir=output.parent) as temp:
        stage = Path(temp) / "package"
        licenses_dir = stage / "licenses"
        licenses_dir.mkdir(parents=True)
        for name, source in inputs.items():
            shutil.copy2(source, stage / name)

        for name in ("COPYING.txt", "MODIFICATIONS.txt"):
            shutil.copyfile(repo_root / name, stage / name)

        license_rows: list[tuple[str, str]] = []
        for relative, source in dependency_licenses(repo_root):
            leaf = source.name
            name = hashlib.sha256(relative.encode("utf-8")).hexdigest()[:10] + "-" + leaf
            destination = licenses_dir / name
            shutil.copy2(source, destination)
            license_rows.append((relative, f"licenses/{name}"))

        for name, notice in dependencies["notices"].items():
            shutil.copyfile(dependency_root / notice["path"], licenses_dir / name)
            license_rows.append(("Pinned dependency notice: " + name, "licenses/" + name))
        (licenses_dir / "concurrentqueue.txt").write_text(queue_text, encoding="utf-8")
        license_rows.append(("Compiled concurrentqueue headers (BSD/Zlib)", "licenses/concurrentqueue.txt"))
        public_dependencies = {key: value for key, value in dependencies.items()
                               if key not in {"paths", "notices"}}
        public_dependencies["concurrentqueue_headers"] = queue_hashes
        public_dependencies["notices"] = {name: record["sha256"] for name, record in dependencies["notices"].items()}
        (stage / "dependency-provenance.json").write_text(
            json.dumps(public_dependencies, indent=2) + "\n", encoding="utf-8")

        vc_notice = """Microsoft Visual C++ Runtime components

This package contains unmodified Microsoft Visual C++ 2015-2022 x64 runtime
files: MSVCP140.dll, VCRUNTIME140.dll, and VCRUNTIME140_1.dll. They were copied
from the Microsoft Visual Studio x64 CRT redistributable directory.

Redistribution is subject to the applicable Microsoft Visual Studio license
terms. See Microsoft's current redistribution guidance:
https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170
and the license terms for the Visual Studio edition used to build this package.
"""
        (licenses_dir / "Microsoft-VCRedist.txt").write_text(vc_notice, encoding="utf-8")
        license_rows.append(("Microsoft Visual C++ Runtime notice", "licenses/Microsoft-VCRedist.txt"))

        (stage / "README.txt").write_text(
            "RAGE WARS RECOMPILED - v0.5.0-beta.1\nXR64 Studios / Windows x64 / non-commercial fan recompilation\n\nINSTALL OR UPDATE\nClose Rage Wars and back up your existing profile. Extract the complete\nBeta 0.5.0 Windows ZIP into a new folder and launch RageWarsRecompiled.exe.\nKeep all included DLLs together. Update your shortcut to the new executable.\nRetain the old program folder and the profile backup as a fallback.\n\nSaves/settings use %LOCALAPPDATA%\\XR64\\RageWars, with game data under\nuser-data. Do not delete that profile folder. XR64_PROFILE_DIR remains an\noptional override for a separate profile. This is a complete program update;\nno updater, binary patch, or save-location migration is required.\n\nSelect your own supported Turok: Rage Wars US v1.0 ROM in setup and choose PC\nfor desktop play. START SCREEN: Press Escape to advance.\nUse Pause > Options: PC OPTIONS for display, controls, input devices and weapon\nmodels, and VR OPTIONS for headset settings (title/lobby Options shows both).\n\nBeta 0.5.0 turns on, by default, smoother high-refresh presentation (an 80 ms\nvisual delay with body and animation smoothing), native player view and ground\nmovement, a texture filter for guns and models, the colour combiner and\ntwo-colour HUD text, and a reworked VR layer (level play space, head-locked HUD,\nlaser sight, analog movement and smooth turning). Each can be switched off by\nsetting its environment variable to 0; the release page lists them. Exact-package\ntest results and remaining beta limits are recorded on the release page. Ordinary error reports retain the version, executable\nidentity and useful failure details. No development frame or RAM captures,\ninput replay, or call journals are enabled in the consumer package.\n\nExperimental PCVR requires a separately installed active OpenXR runtime.\nSelect Auto input for Touch. Calibrated models require your own extracted\nmodels and calibration files; automatic extraction is unavailable.\nThere is no standalone Quest build. Headset tracking, weapon attachment,\nfallback weapons, near-wall behavior and shot impacts remain experimental;\nread the release's tested evidence and known issues before VR play.\n\nThe binary contains translated guest code; source includes reconstruction\nedits derived from the original program. No ROM file, extracted assets,\nsaves, profiles or RAM captures are included. Original game copyrights remain\nwith their respective owners. See COPYING.txt, MODIFICATIONS.txt and licenses/.\nMatching source, rebuild instructions, downloads, bug reports and comments:\nhttps://github.com/ArtfulRascal8/rage-wars-recompiled/releases/tag/v0.5.0-beta.1\n\nFile hashes are in package-manifest.json; pinned public dependency identities\nare in dependency-provenance.json. Keep included runtime DLLs beside the EXE.\n", encoding="utf-8")

        notices = [
            "Rage Wars Recompiled consumer package - third-party notices",
            "",
            "This package contains RageWarsRecompiled.exe, SDL2.dll, openxr_loader.dll, app-local Microsoft Visual C++ runtime DLLs, documentation, and license texts.",
            "No ROM file, extracted assets, Controller Pak, user configuration, PDB, capture, or OpenXR vendor runtime is included; the executable contains translated guest code.",
            "",
            "N64ModernRuntime and N64Recomp dependency license files are copied into licenses/.",
            "nlohmann JSON 3.12.0 (Niels Lohmann), Ares RSP vector implementation (ares team, Near et al), and odashi/encoding EUC-JP conversion (Yusuke Oda): full notices in licenses/.",
            "GNU GPLv3, dated modifications and matching-source access: COPYING.txt and MODIFICATIONS.txt.",
            "SDL2 2.30.11: official SDL VC archive, x64 DLL unchanged; license in licenses/SDL2.txt.",
            "Microsoft Visual C++ runtime DLLs are copied unmodified from the installed x64 CRT redistributable; see licenses/Microsoft-VCRedist.txt for the notice and redistribution guidance.",
            "OpenXR 1.1.58: built from KhronosGroup/OpenXR-SDK commit " + dependencies["openxr"]["commit"] + "; source unmodified.",
            "Loader/headers: Apache-2.0 OR MIT. Full licenses, per-file attribution and vendored JsonCpp license are in licenses/OpenXR-*.txt.",
            "moodycamel concurrentqueue and its semaphore adaptation: notices retained in licenses/concurrentqueue.txt.",
            "Exact shipped DLL hashes, source/archive pins and build options: dependency-provenance.json.",
            "The OpenXR runtime is provided and installed separately by the end user.",
            "",
            "Dependency license file map:",
        ]
        notices.extend(f"  {source} -> {destination}" for source, destination in license_rows)
        (stage / "THIRD-PARTY-NOTICES.txt").write_text("\n".join(notices) + "\n", encoding="utf-8")

        files = {
            path.relative_to(stage).as_posix(): sha256(path)
            for path in sorted(stage.rglob("*"))
            if path.is_file()
        }
        manifest = {
            "schema": "xr64.rage-wars.package.v1",
            "executable": EXECUTABLE,
            "version": "0.5.0-beta.1",
            "baseline_id": "C0F87F11",
            "files": files,
        }
        (stage / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        audit_package_content(stage)

        backup = Path(temp) / "previous-package"
        if output.exists():
            os.replace(output, backup)
        try:
            os.replace(stage, output)
        except Exception:
            if backup.exists() and not output.exists():
                os.replace(backup, output)
            raise

    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--sdl2-include", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dependencies", type=Path, required=True)
    parser.add_argument("--staging", action="store_true", help="Allow a build-local package artifact, not a runtime installation")
    args = parser.parse_args()
    result = build_package(args.repo_root, args.exe, args.runtime_dir, args.sdl2_include, args.output, dependency_root=args.dependencies, staging=args.staging)
    print(json.dumps({"output": str(args.output.resolve()), "file_count": len(result["files"])}, indent=2))


if __name__ == "__main__":
    main()