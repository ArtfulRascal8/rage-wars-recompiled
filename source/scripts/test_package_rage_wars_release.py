#!/usr/bin/env python3
"""Focused package-safety contracts; uses fake PE files and temporary folders only."""
from __future__ import annotations

import json
import functools
import hashlib
import shutil
from pathlib import Path
import struct
import tempfile
import unittest

from package_rage_wars_release import MANIFEST, audit_package_content, build_package, package_inventory, verify_owned_package


def fake_x64_pe(path: Path, machine: int = 0x8664) -> None:
    data = bytearray(128)
    data[:2] = b"MZ"
    data[0x3C:0x40] = struct.pack("<I", 0x40)
    data[0x40:0x44] = b"PE\0\0"
    data[0x44:0x46] = struct.pack("<H", machine)
    path.write_bytes(data)


class ReleasePackageContracts(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        (self.root / "toolchain/N64ModernRuntime").mkdir(parents=True)
        (self.root / "toolchain/N64Recomp").mkdir(parents=True)
        (self.root / "toolchain/N64ModernRuntime/COPYING").write_text("runtime license", encoding="utf-8")
        (self.root / "toolchain/N64Recomp/LICENSE").write_text("recompiler license", encoding="utf-8")
        self.runtime = self.root / "binary"
        self.runtime.mkdir()
        fake_x64_pe(self.runtime / "RageWarsRecompiled.exe")
        fake_x64_pe(self.runtime / "SDL2.dll")
        fake_x64_pe(self.runtime / "openxr_loader.dll")
        fake_x64_pe(self.runtime / "MSVCP140.dll")
        fake_x64_pe(self.runtime / "VCRUNTIME140.dll")
        fake_x64_pe(self.runtime / "VCRUNTIME140_1.dll")
        self.sdl_include = self.root / "sdl-include"
        self.sdl_include.mkdir()
        (self.sdl_include / "SDL.h").write_text("/* SDL license text */\\n", encoding="utf-8")
        self.dependencies = self.root / "dependencies"
        self.dependencies.mkdir()
        (self.dependencies / "include").mkdir()
        shutil.copyfile(self.sdl_include / "SDL.h", self.dependencies / "include/SDL.h")
        self.sdl_include = self.dependencies / "include"
        scripts = self.root / "scripts"
        scripts.mkdir()
        h = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        lock = {"sdl2": {"version": "2.30.11", "dll_sha256": h(self.runtime / "SDL2.dll")},
                "openxr": {"version": "1.1.58", "commit": "fixture"}}
        lock_path = scripts / "pc_dependencies.lock.json"
        lock_path.write_text(json.dumps(lock))
        artifacts = {}
        for key, filename in (("sdl_dll", "SDL2.dll"), ("xr_dll", "openxr_loader.dll")):
            shutil.copyfile(self.runtime / filename, self.dependencies / filename)
            artifacts[key] = {"path": filename, "sha256": h(self.dependencies / filename)}
        (self.dependencies / "notices").mkdir()
        notices = {}
        for name in ("SDL2.txt", "OpenXR-MIT.txt", "OpenXR-Apache-2.0.txt", "OpenXR-JsonCpp.txt", "OpenXR-ATTRIBUTION.txt"):
            path = self.dependencies / "notices" / name
            path.write_text("fixture notice " + name)
            notices[name] = {"path": "notices/" + name, "sha256": h(path)}
        data = {"schema": "xr64.pc-dependencies.v1", "lock_sha256": h(lock_path),
                "sdl2": lock["sdl2"], "openxr": lock["openxr"],
                "paths": {"sdl_include": "include"}, "artifacts": artifacts, "notices": notices}
        (self.dependencies / "dependencies.json").write_text(json.dumps(data))
        queue = self.root / "toolchain/N64ModernRuntime/thirdparty/concurrentqueue"
        queue.mkdir(parents=True)
        for header in ("concurrentqueue.h", "blockingconcurrentqueue.h"):
            (queue / header).write_text("// Fixture BSD notice\n#pragma once\n")
        (queue / "lightweightsemaphore.h").write_text("// Fixture adaptation\n#pragma once\n// LICENSE:\n// Fixture Zlib notice\n#if defined(_WIN32)\n")
        for relative in (
            "COPYING.txt", "MODIFICATIONS.txt",
            "toolchain/N64ModernRuntime/thirdparty/json/LICENSE.MIT",
            "toolchain/N64ModernRuntime/librecomp/include/librecomp/LICENSE-Ares-RSP.txt",
            "toolchain/N64ModernRuntime/librecomp/src/LICENSE-odashi-encoding.txt",
            "toolchain/N64ModernRuntime/LICENSE-Fast3D-reference.txt",
        ):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Fixture full notice\n", encoding="utf-8")
        self.build_package = functools.partial(build_package, dependency_root=self.dependencies)
        self.output = Path(self.temp.name) / "portable-package"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_package_contains_only_allowlisted_runtime_and_notice_files(self) -> None:
        result = self.build_package(
            self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime, self.sdl_include, self.output
        )
        verify_owned_package(self.output)
        inventory = audit_package_content(self.output)
        self.assertTrue(any(path.startswith("licenses/") for path in inventory))
        self.assertIn("RageWarsRecompiled.exe", result["files"])
        self.assertIn("SDL2.dll", result["files"])
        self.assertIn("openxr_loader.dll", result["files"])
        self.assertIn("MSVCP140.dll", result["files"])
        self.assertIn("VCRUNTIME140.dll", result["files"])
        self.assertIn("VCRUNTIME140_1.dll", result["files"])
        self.assertIn("licenses/SDL2.txt", result["files"])
        self.assertIn("licenses/Microsoft-VCRedist.txt", result["files"])
        self.assertIn("licenses/OpenXR-MIT.txt", result["files"])
        self.assertIn("licenses/OpenXR-JsonCpp.txt", result["files"])
        self.assertIn("licenses/concurrentqueue.txt", result["files"])
        self.assertIn("dependency-provenance.json", result["files"])
        self.assertIn("COPYING.txt", result["files"])
        self.assertIn("MODIFICATIONS.txt", result["files"])
        self.assertNotIn("controller.pak", package_inventory(self.output))
        self.assertNotIn("rom.z64", package_inventory(self.output))
        self.assertNotIn("rage_wars.pdb", package_inventory(self.output))
        self.assertEqual(json.loads((self.output / MANIFEST).read_text())["schema"], "xr64.rage-wars.package.v1")

    def test_missing_compiled_component_notice_stops_packaging(self) -> None:
        (self.root / "toolchain/N64ModernRuntime/thirdparty/json/LICENSE.MIT").unlink()
        with self.assertRaisesRegex(FileNotFoundError, "Required release notice"):
            self.build_package(self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime,
                               self.sdl_include, self.output)
        self.assertFalse(self.output.exists())

    def test_owned_package_can_be_rebuilt_idempotently(self) -> None:
        args = (self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime, self.sdl_include, self.output)
        self.build_package(*args)
        first = json.loads((self.output / MANIFEST).read_text())
        self.build_package(*args)
        second = json.loads((self.output / MANIFEST).read_text())
        self.assertEqual(first, second)

    def test_missing_app_local_crt_dependency_stops_packaging(self) -> None:
        (self.runtime / "VCRUNTIME140_1.dll").unlink()
        with self.assertRaisesRegex(FileNotFoundError, "VCRUNTIME140_1.dll"):
            self.build_package(
                self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime,
                self.sdl_include, self.output
            )
        self.assertFalse(self.output.exists())

    def test_package_must_be_outside_repository(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside the repository"):
            self.build_package(
                self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime,
                self.sdl_include, self.root / "dist/package"
            )

    def test_untracked_existing_package_content_is_preserved_and_refused(self) -> None:
        self.build_package(self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime, self.sdl_include, self.output)
        extra = self.output / "user-data" / "controller.pak"
        extra.parent.mkdir()
        extra.write_bytes(b"user save")
        with self.assertRaisesRegex(ValueError, "outside the file allowlist|untracked or missing"):
            self.build_package(self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime, self.sdl_include, self.output)
        self.assertEqual(extra.read_bytes(), b"user save")

    def test_game_data_marker_inside_nested_license_is_rejected(self) -> None:
        marker = self.root / "toolchain/N64Recomp/LICENSE-private"
        marker.write_bytes(b"RAGE_WARS_PRIVATE_GAME_DATA")
        with self.assertRaisesRegex(ValueError, "ROM/game-data marker"):
            self.build_package(
                self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime,
                self.sdl_include, self.output
            )
        self.assertFalse(self.output.exists())
    def test_dependency_dll_mismatch_is_rejected_before_staging(self) -> None:
        (self.runtime / "openxr_loader.dll").write_bytes((self.runtime / "openxr_loader.dll").read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "DLL differs"):
            self.build_package(self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime, self.sdl_include, self.output)
        self.assertFalse(self.output.exists())

    def test_missing_or_changed_dependency_notice_is_rejected(self) -> None:
        (self.dependencies / "notices/OpenXR-MIT.txt").write_text("changed")
        with self.assertRaisesRegex(ValueError, "artifact/notice identity mismatch"):
            self.build_package(self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime, self.sdl_include, self.output)
        self.assertFalse(self.output.exists())

    def test_non_x64_binary_is_rejected(self) -> None:
        fake_x64_pe(self.runtime / "openxr_loader.dll", 0x14C)
        with self.assertRaisesRegex(ValueError, "Windows x64"):
            self.build_package(
                self.root, self.runtime / "RageWarsRecompiled.exe", self.runtime,
                self.sdl_include, self.output
            )


if __name__ == "__main__":
    unittest.main()