#!/usr/bin/env python3
"""Acquire hash-pinned public PC dependencies; build the unmodified OpenXR loader."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

LOCK_PATH = Path(__file__).with_name("pc_dependencies.lock.json")

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(condition, message):
    if not condition:
        raise ValueError(message)

def acquire(root, name, spec, offline):
    archive = root / "archives" / (name + ".zip")
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        require(not offline, "Offline archive is missing: " + str(archive))
        temporary = archive.with_suffix(".download")
        try:
            request = urllib.request.Request(spec["url"], headers={"User-Agent": "XR64-PC-dependencies"})
            with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as dest:
                shutil.copyfileobj(response, dest)
            require(digest(temporary) == spec["archive_sha256"], name + " archive SHA-256 mismatch")
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    require(digest(archive) == spec["archive_sha256"], name + " archive SHA-256 mismatch")
    source = root / "sources" / spec["directory"]
    source.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as zipped:
        members = [item for item in zipped.infolist() if not item.is_dir()]
        for item in members:
            parts = Path(item.filename).parts
            require(parts and parts[0] == spec["directory"] and ".." not in parts,
                    "Unsafe archive member: " + item.filename)
            require(not Path(item.filename).is_absolute() and
                    (item.external_attr >> 16 & 0o170000) != 0o120000,
                    "Archive link/absolute path is forbidden")
        if source.exists():
            expected = set()
            for item in members:
                path = root / "sources" / item.filename
                expected.add(path.resolve())
                require(path.is_file() and not path.is_symlink() and path.read_bytes() == zipped.read(item),
                        "Dependency source cache changed: " + item.filename)
            actual = {p.resolve() for p in source.rglob("*") if p.is_file()}
            require(actual == expected, "Unexpected file in dependency source cache")
        else:
            with tempfile.TemporaryDirectory(dir=source.parent, prefix=".extract-") as temp:
                zipped.extractall(temp)
                os.replace(Path(temp) / spec["directory"], source)
    return source

def run_logged(command, log):
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    require(result.returncode == 0, "Dependency command failed; see " + str(log))

def prepare(root, cmake, offline=False):
    root = root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    sdl = acquire(root, "sdl2", lock["sdl2"], offline)
    xr = acquire(root, "openxr", lock["openxr"], offline)
    loader_build = root / "openxr-build"
    options = ["-D" + key + "=" + value for key, value in lock["openxr"]["options"].items()]
    run_logged([cmake, "-S", str(xr), "-B", str(loader_build), "-G", lock["openxr"]["generator"],
                "-A", "x64", *options], root / "openxr-configure.log")
    run_logged([cmake, "--build", str(loader_build), "--config", "Release",
                "--target", "openxr_loader", "--parallel", "4"], root / "openxr-build.log")
    paths = {
        "sdl_include": sdl / "include", "sdl_dll": sdl / "lib/x64/SDL2.dll",
        "xr_include": xr / "include",
        "xr_library": loader_build / "src/loader/Release/openxr_loader.lib",
        "xr_dll": loader_build / "src/loader/Release/openxr_loader.dll",
    }
    for key, path in paths.items():
        require(path.exists() and path.resolve().is_relative_to(root), "Dependency missing/outside cache: " + key)
    require(digest(paths["sdl_dll"]) == lock["sdl2"]["dll_sha256"], "SDL2 DLL identity mismatch")
    cache = (loader_build / "CMakeCache.txt").read_text(encoding="utf-8")
    toolchain = {}
    for line in cache.splitlines():
        if line.startswith(("CMAKE_GENERATOR:", "CMAKE_GENERATOR_PLATFORM:", "CMAKE_VS_PLATFORM_TOOLSET:",
                            "CMAKE_VS_PLATFORM_TOOLSET_VERSION:", "CMAKE_SYSTEM_VERSION:")):
            toolchain[line.split(":", 1)[0]] = line.split("=", 1)[1]
    import re
    import xml.etree.ElementTree as ET
    for compiler_file in loader_build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"):
        content = compiler_file.read_text(encoding="utf-8")
        for key in ("CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION"):
            match = re.search(r'set\(' + key + r' "([^"]+)"\)', content)
            if match:
                toolchain[key] = match.group(1)
    project = loader_build / "src/loader/openxr_loader.vcxproj"
    for node in ET.parse(project).iter():
        tag = node.tag.rsplit("}", 1)[-1]
        if tag in {"WindowsTargetPlatformVersion", "PlatformToolset"}:
            toolchain[tag] = node.text
    notice_dir = root / "notices"
    notice_dir.mkdir(exist_ok=True)
    notices = {
        "SDL2.txt": sdl / "LICENSE.txt",
        "OpenXR-MIT.txt": xr / "LICENSES/MIT.txt",
        "OpenXR-Apache-2.0.txt": xr / "LICENSES/Apache-2.0.txt",
        "OpenXR-JsonCpp.txt": xr / "src/external/jsoncpp/LICENSE",
    }
    notice_files = []
    for folder in ("src/loader", "src/common", "include/openxr"):
        for extension in ("*.h", "*.hpp", "*.cpp"):
            notice_files.extend((xr / folder).glob(extension))
    notice_files += [xr / "src/xr_generated_loader.cpp", xr / "src/xr_generated_loader.hpp", xr / "src/common_config.h"]
    copyrights = sorted({line.strip().strip("/* ") for path in notice_files if path.is_file()
                         for line in path.read_text(encoding="utf-8").splitlines()[:30] if "Copyright" in line})
    description = ("OpenXR loader built from unmodified KhronosGroup/OpenXR-SDK " + lock["openxr"]["tag"] + "\n"
                   + "Commit: " + lock["openxr"]["commit"] + "\n"
                   + "Loader/headers: Apache-2.0 OR MIT; full texts retained.\n"
                   + "Vendored JsonCpp 1.9.6: MIT OR public domain; full upstream license retained.\n"
                   + "Windows x64 Release shared loader; vendored JsonCpp, MSVC DLL CRT.\n"
                   + "Built here from source; no SteamVR loader binary is copied.\n\n" + "\n".join(copyrights) + "\n")
    (notice_dir / "OpenXR-ATTRIBUTION.txt").write_text(description, encoding="utf-8")
    notices["OpenXR-ATTRIBUTION.txt"] = notice_dir / "OpenXR-ATTRIBUTION.txt"
    for name, source in notices.items():
        require(source.is_file(), "Missing dependency notice: " + str(source))
        if source.parent != notice_dir:
            shutil.copyfile(source, notice_dir / name)
    result = {
        "schema": "xr64.pc-dependencies.v1", "lock_sha256": digest(LOCK_PATH),
        "paths": {key: path.relative_to(root).as_posix() for key, path in paths.items()},
        "artifacts": {key: {"path": path.relative_to(root).as_posix(), "sha256": digest(path)}
                      for key, path in paths.items() if path.is_file()},
        "sdl2": lock["sdl2"], "openxr": lock["openxr"],
        "cmake_version": subprocess.check_output([cmake, "--version"], text=True).splitlines()[0],
        "toolchain": toolchain,
        "notices": {name: {"path": "notices/" + name, "sha256": digest(notice_dir / name)} for name in notices},
    }
    (root / "dependencies.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    names = {"sdl_include": "RAGE_WARS_SDL2_INCLUDE_DIR", "sdl_dll": "RAGE_WARS_SDL2_DLL",
             "xr_include": "XR64_OPENXR_INCLUDE_DIR", "xr_library": "XR64_OPENXR_LIBRARY",
             "xr_dll": "XR64_OPENXR_LOADER"}
    lines = ["# Generated from hash-verified public dependencies; local cache paths only."]
    for key, variable in names.items():
        lines.append('set(' + variable + ' "' + (root / result["paths"][key]).as_posix() + '")')
    (root / "dependencies.cmake").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--offline", action="store_true", help="Use verified cached archives; no downloads")
    args = parser.parse_args()
    result = prepare(args.output, args.cmake, args.offline)
    print(json.dumps({"sdl2": result["sdl2"]["version"], "openxr": result["openxr"]["version"],
                      "loader_sha256": result["artifacts"]["xr_dll"]["sha256"]}))

if __name__ == "__main__":
    main()
