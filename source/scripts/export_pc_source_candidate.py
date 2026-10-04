#!/usr/bin/env python3
"""Export the audited working source and reviewed reconstruction recipe, never Git history."""
import argparse
import shutil
from pathlib import Path
from audit_pc_dependency_inputs import project_inputs, read_inputs
from reconstruction_common import digest, load, require, save, safe_path, verify_source_candidate

SCRIPTS = (
    "reconstruction_common.py", "reconstruct_pc_candidate.py", "export_pc_source_candidate.py",
    "prepare_pc_reconstruction_recipe.py", "test_reconstruction.py",
    "audit_pc_dependency_inputs.py", "audit_demo_build.py", "audit_release_privacy.py",
    "build_pc_candidate.ps1", "prepare_pc_dependencies.py", "pc_dependencies.lock.json",
    "package_rage_wars_release.py", "test_package_rage_wars_release.py", "test_pc_dependencies.py",
    "test_crash_reporting.py",
    "prepare_demo_source.py", "prepare_recomp_host_overlay.py", "regenerate_audio_rsp.py",
    "audio_rsp_manifest.json", "apply_modern_controls_hooks.py", "modern_controls_hooks.json",
    "instrument_damage_reversal.py",
)
FORBIDDEN = {".exe", ".dll", ".pdb", ".obj", ".os", ".z64", ".v64", ".n64", ".bin",
             ".pak", ".zip", ".dmp", ".png", ".gif", ".tif", ".docx", ".ai"}

def audited_inputs(build):
    require(build.is_dir(), "Audited build directory missing: " + str(build))
    declared, reads = project_inputs(build), read_inputs(build)
    require(bool(declared) and bool(reads), "Build projects and actual read logs are required: " + str(build))
    return declared | reads

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--repo", type=Path, required=True)
    p.add_argument("--recipe", type=Path, required=True)
    p.add_argument("--baseline-build", type=Path, required=True)
    p.add_argument("--generator-build", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    repo, recipe, output = a.repo.resolve(), a.recipe.resolve(), a.output.resolve()
    require(not output.exists(), "Export destination must be new")
    candidates = audited_inputs(a.baseline_build)
    candidates |= audited_inputs(a.generator_build)
    for build in (a.baseline_build, a.generator_build):
        for dependency in build.rglob("generate.stamp.depend"):
            for line in dependency.read_text(encoding="utf-8").splitlines():
                path = Path(line)
                if path.is_absolute():
                    candidates.add(path.resolve())
    for audit in a.baseline_build.rglob("*.audit.json"):
        candidates.add(Path(load(audit)["source"]).resolve())
    # Configure requires CMake files and declared targets beyond the selected runtime tests.
    for base in (repo / "toolchain", repo / "staging/rage-wars-recomp-spike/pc_runtime"):
        candidates.update(base.rglob("CMakeLists.txt"))
        candidates.update(base.rglob("*.cmake"))
    for base in (repo / "toolchain/N64Recomp", repo / "toolchain/N64ModernRuntime"):
        for path in base.rglob("*"):
            if path.is_file() and path.name.lower().startswith(("license", "copying")):
                candidates.add(path)
    candidates.update(repo / "scripts" / name for name in SCRIPTS)
    candidates.add(repo / "docs/PC_RECONSTRUCTION.md")
    candidates.update(repo / name for name in ("README.md", "COPYING.txt", "MODIFICATIONS.txt"))
    candidates.add(repo / "staging/rage-wars-recomp-spike/pc_runtime/tools/build_generated_source_manifest.py")
    for name in ("configs/rage_wars.us.ares_boot_to_combat.toml",
                 "symbols/rage_wars.us.ares_boot_to_combat.toml"):
        candidates.add(repo / "staging/rage-wars-recomp-spike" / name)
    candidates.add(repo / "staging/rage-wars-recomp-spike/pc_runtime/cmake/rage_wars_generated_source_manifest.json")
    selected = {}
    for path in sorted(candidates):
        path = path.resolve()
        if not path.is_relative_to(repo):
            continue  # Installed compiler/Windows inputs are recorded by the build audit.
        relative = path.relative_to(repo)
        if relative.parts[0] == "build" or relative.as_posix().startswith("staging/rage-wars-recomp-spike/generated/") or ".git" in relative.parts:
            continue
        if relative.as_posix() == "staging/rage-wars-recomp-spike/pc_runtime/cmake/rage_wars_generated_sources.cmake":
            continue  # Stale generated inventory; configure regenerates it inside the new build directory.
        if path.is_dir():
            continue  # Include/search directories are represented by their actual file reads.
        require(path.is_file() and not path.is_symlink(), "Source allowlist entry missing/link: " + str(relative))
        require(path.suffix.lower() not in FORBIDDEN, "Binary/private file in source allowlist: " + str(relative))
        path.read_text(encoding="utf-8")  # Private candidate contains reviewed text sources/configuration only.
        selected[relative.as_posix()] = path
    for path in sorted(recipe.iterdir()):
        require(path.is_file() and not path.is_symlink(), "Recipe entry must be a regular text file")
        require(path.suffix in {".json", ".patch", ".md"}, "Unexpected recipe payload")
        path.read_text(encoding="utf-8")
        selected["reconstruction/" + path.name] = path
    require("reconstruction/reconstruction.lock.json" in selected, "Reviewed reconstruction lock missing")
    output.mkdir(parents=True)
    files = {}
    for relative, source in sorted(selected.items()):
        destination = safe_path(output, relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        raw = source.read_bytes()
        destination.write_bytes(raw)
        files[relative] = {"sha256": digest(raw), "bytes": len(raw),
                          "origin": "private-recipe" if relative.startswith("reconstruction/") else "working-source"}
    # The old inventory's absolute checkout hint has no role in --verify. Remove it from this export only.
    inventory_path = output / "staging/rage-wars-recomp-spike/pc_runtime/cmake/rage_wars_generated_source_manifest.json"
    inventory = load(inventory_path)
    inventory["generated_dir"] = "../generated/ares_boot_to_combat"
    save(inventory_path, inventory)
    relative = inventory_path.relative_to(output).as_posix()
    files[relative]["sha256"] = digest(inventory_path.read_bytes())
    files[relative]["bytes"] = inventory_path.stat().st_size
    files[relative]["export_edit"] = "Portable inventory hint; source names/count/digest unchanged."
    manifest = {
        "schema": "xr64.private-reconstruction-source.v1", "private_review_only": True,
        "baseline_fallback_executable": "7E40CAEE75F42D69D4CD38B13E3B4952B4F10691663AA464E5E8F3327E1CBE77",
        "allowlist_basis": "Declared projects, tracked reads, original consumer-source receipts, configure inputs, named bootstrap/contracts/notices.",
        "files": files,
        "excluded": ["Git history", "generated guest/audio outputs", "ROM", "captures", "saves",
                     "profiles", "extracted assets", "build products", "unrelated development output"],
    }
    save(output / "source-manifest.json", manifest)
    verify_source_candidate(output)
    print("Exported", len(files), "explicit, hash-verified working-source/recipe files.")

if __name__ == "__main__":
    main()

