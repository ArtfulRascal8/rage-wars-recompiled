#!/usr/bin/env python3
"""Overlay reviewed program/notice files into an existing TESTING slot; retain a rollback copy."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import os
from package_rage_wars_release import MANIFEST, EXECUTABLE, sha256, verify_owned_package

def deploy(package, testing, backup, expected):
    package, testing, backup = package.resolve(), testing.resolve(), backup.resolve()
    verify_owned_package(package)
    if not testing.is_dir() or testing.is_relative_to(package) or package.is_relative_to(testing):
        raise ValueError("TESTING must be an existing separate slot")
    old = json.loads((testing / MANIFEST).read_text(encoding="utf-8"))
    if old.get("schema") != "xr64.rage-wars.package.v1" or old.get("executable") != "rage_wars.exe":
        raise ValueError("Existing TESTING manifest identity is unsupported")
    if sha256(testing / "rage_wars.exe") != expected.upper():
        raise ValueError("TESTING baseline hash differs; no overwrite performed")
    for relative, wanted in old["files"].items():
        path = (testing / relative).resolve()
        if not path.is_relative_to(testing) or not path.is_file() or sha256(path) != wanted:
            raise ValueError("Existing TESTING file differs from its manifest: " + relative)
    if backup.exists():
        raise FileExistsError("Rollback directory must be new")
    new = json.loads((package / MANIFEST).read_text(encoding="utf-8"))
    replacements = {("rage_wars.exe" if name == EXECUTABLE else name): package / name for name in new["files"]}
    backup.mkdir(parents=True)
    existed = {}
    for relative in list(replacements) + [MANIFEST]:
        target = testing / relative
        existed[relative] = target.is_file()
        if target.is_file():
            dest = backup / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, dest)
    changed = []
    try:
        for relative, source in replacements.items():
            dest = testing / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=dest.parent, delete=False, prefix=".dependency-stage-") as temp:
                pending = Path(temp.name)
            try:
                shutil.copyfile(source, pending)
                if relative == "README.txt":
                    text = pending.read_text(encoding="utf-8").replace(EXECUTABLE, "rage_wars.exe")
                    pending.write_text("TESTING SLOT - REVIEWED CANDIDATE\n\n" + text, encoding="utf-8")
                os.replace(pending, dest)
                changed.append(relative)
            finally:
                pending.unlink(missing_ok=True)
        combined = dict(old["files"])
        combined.update({name: sha256(testing / name) for name in replacements})
        manifest = dict(old)
        manifest["files"] = combined
        manifest["dependency_repair_candidate"] = {"source_executable": EXECUTABLE,
            "sha256": sha256(testing / "rage_wars.exe"), "version": new["version"]}
        with tempfile.NamedTemporaryFile(dir=testing, delete=False, prefix=".manifest-stage-") as temp:
            pending = Path(temp.name)
        try:
            pending.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            os.replace(pending, testing / MANIFEST)
            changed.append(MANIFEST)
        finally:
            pending.unlink(missing_ok=True)
        for name, wanted in combined.items():
            if sha256(testing / name) != wanted:
                raise ValueError("Deployed file differs: " + name)
    except Exception:
        for name in reversed(changed):
            if existed[name]:
                shutil.copy2(backup / name, testing / name)
            else:
                (testing / name).unlink(missing_ok=True)
        raise
    evidence = {"testing_executable_sha256": sha256(testing / "rage_wars.exe"),
                "previous_executable_sha256": expected.upper(), "replacement_files": sorted(replacements),
                "preserved_manifest_files": sorted(set(old["files"]) - set(replacements)),
                "backed_up_files": sorted(name for name, exists in existed.items() if exists)}
    (backup / "deployment.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    return evidence

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--testing-slot", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--expected-executable-sha256", required=True)
    args = parser.parse_args()
    result = deploy(args.package, args.testing_slot, args.backup, args.expected_executable_sha256)
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()
