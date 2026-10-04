#!/usr/bin/env python3
"""Reconstruct and build the public PC release from reviewed source and a user-supplied ROM."""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path
sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
from reconstruction_common import (
    apply_edit, digest, load, require, save, safe_path, split_functions,
    verify_inventory, verify_source_candidate, write_candidate,
)
from regenerate_audio_rsp import verify_rom

ROM_SHA256 = "0433043aaba2649bdd1fe717c4020550ac663c0362527aa082490f5977a3e46b"
AUTHORING_GENERATOR_SHA256 = "b47af74141c71b2aad45088254865b4ed31b021bad19dd45a0130bf8690f1ca2"
CLOSURE_PINS = {
    "scripts/gameplay_menu_rom_closure_20261003.json": (
        "9bb65b0430bd3e33d39aff2f319847b5bb3ba74687dbb1d1a7b341490f28636c", 496,
        "e208df091c715458f65a887238aeaf449ef3cb451a5de3d8074c85f7c36201df"),
    "scripts/targeted_jal_rom_closure_20261003.json": (
        "749000e4cc51d9c33afbf4f5fd4ff109b0b47509fd4e33e6cce00c48087ddaf3", 46,
        "ce146a52db4bbebf8e535d310f7d46263eb852f4bc22a7606150ee19e2ba4623"),
    "scripts/targeted_nonreturn_worker_rom_closure_20261003.json": (
        "b2832e7450f1c036c6f336bab2f4f725ced750e697538c1787601033cdf25ea4", 3,
        "c57e028547e702e5bae72e76bde6b1a2884f74471579c933c99ebe8e938a77cc"),
}


def run(command, log, cwd=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(x) for x in command], cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
    require(result.returncode == 0, "Command failed; see " + str(log))


def symbol_identities(path):
    sections = tomllib.loads(path.read_text(encoding="utf-8"))["section"]
    identities = {}
    for section in sections:
        for function in section.get("functions", []):
            name = function["name"]
            identity = (function["vram"], section["rom"] + function["vram"] - section["vram"], function["size"])
            require(name not in identities or identities[name] == identity,
                    "Ambiguous recovered symbol identity: " + name)
            identities[name] = identity
    return identities


def verify_recipe_pins(recipe, lock, rom):
    require(lock["schema"] == "xr64.pc-reconstruction.v2", "Wrong reconstruction lock")
    require(lock["release_version"] == "0.2.0-beta.1", "Wrong reconstruction release version")
    require(lock["canonical_function_count"] == 3751, "Wrong canonical guest function count")
    require(lock["recovered_rom_function_count"] == 545, "Wrong ROM-closed function count")
    require(lock["rom_table_count"] == 60, "Wrong reviewed ROM table count")
    require(digest(rom) == ROM_SHA256 == lock["rom_sha256"], "ROM identity mismatch")
    pins = {
        "canonical-functions.json": "canonical_function_manifest_sha256",
        "rom-function-recoveries.json": "rom_function_recoveries_sha256",
        "rom-tables.json": "rom_tables_sha256",
        "rom-table-provenance.json": "rom_table_provenance_sha256",
        "generator-local.patch": "generator_local_patch_sha256",
        "generator-provenance.json": "generator_provenance_sha256",
        "closure-generator-replay.json": "closure_generator_replay_sha256",
        "recovery496-full-generator-differences.json": "recovery496_difference_json_sha256",
        "recovery496-full-generator-differences.md": "recovery496_difference_markdown_sha256",
    }
    for name, lock_key in pins.items():
        path = recipe / name
        require(path.is_file() and digest(path.read_bytes()) == lock[lock_key],
                "Reviewed recipe file digest differs: " + name)

    tables = load(recipe / "rom-tables.json")
    provenance = load(recipe / "rom-table-provenance.json")
    require(len(tables) == lock["rom_table_count"] == provenance["table_pin_total"],
            "Reviewed table union count differs")
    require(provenance["legacy_table_count"] == 29, "Original 29 ROM table pins were not retained")
    require(provenance["rom_sha256"] == digest(rom), "ROM table provenance identity differs")
    for row in tables:
        start, size = row["rom"], row["size"]
        require(0 <= start and 0 < size and start + size <= len(rom), "ROM table bounds differ: " + row["name"])
        require(digest(rom[start:start + size]) == row["sha256"], "ROM table identity differs: " + row["name"])

    recoveries = load(recipe / "rom-function-recoveries.json")
    require(recoveries["schema"] == "xr64.pc-rom-function-recoveries.v1", "Wrong ROM recovery manifest")
    require(recoveries["rom_sha256"] == digest(rom), "ROM recovery manifest ROM identity differs")
    require(recoveries["authoring_generator_sha256"] == AUTHORING_GENERATOR_SHA256,
            "Original recovery authoring-generator pin differs")
    require(len(recoveries["functions"]) == recoveries["function_count"] == lock["recovered_rom_function_count"],
            "ROM recovery manifest function count differs")
    closures = {item["source_configuration"]: item for item in recoveries["source_closures"]}
    require(set(closures) == set(CLOSURE_PINS), "ROM recovery closure inventory differs")
    for relative, (expected_sha, expected_count, expected_output_sha) in CLOSURE_PINS.items():
        row = closures[relative]
        require(row["source_configuration_sha256"] == expected_sha,
                "Frozen closure source hash differs: " + relative)
        require(row["authoring_generator_sha256"] == AUTHORING_GENERATOR_SHA256,
                "Frozen closure authoring-generator differs: " + relative)
        require(row["function_count"] == expected_count, "Frozen closure function count differs: " + relative)
        require(row["saved_generated_sha256"] == expected_output_sha,
                "Frozen closure generated-source digest differs: " + relative)
    replay_receipt = load(recipe / "closure-generator-replay.json")
    require(replay_receipt["rom_sha256"] == digest(rom), "Closure replay receipt ROM differs")
    require({row["source_configuration"]: row["reconstructed_generated_sha256"]
             for row in replay_receipt["closures"]} ==
            {relative: values[2] for relative, values in CLOSURE_PINS.items()},
            "Closure replay receipt output hashes differ")
    replay_recipes = {row["source_configuration"]: row
                      for row in recoveries["closure_generation_recipes"]}
    require(set(replay_recipes) == set(CLOSURE_PINS), "Closure generator recipe inventory differs")
    recovery_by_name = {row["name"]: row for row in recoveries["functions"]}
    table_provenance = load(recipe / "rom-table-provenance.json")
    pinned_tables = {(row["source_configuration"], row["source_function"], row["table_name"],
                      row["rom"], row["vram"], row["size"], row["sha256"])
                     for row in table_provenance["table_pins"]}
    for relative, (expected_sha, expected_count, expected_output_sha) in CLOSURE_PINS.items():
        replay = replay_recipes[relative]
        require(replay["source_configuration_sha256"] == expected_sha and
                replay["saved_generated_sha256"] == expected_output_sha and
                replay["authoring_generator_sha256"] == AUTHORING_GENERATOR_SHA256,
                "Closure generator recipe identity differs: " + relative)
        require(replay["functions_per_output_file"] == 500 and
                len(replay["functions"]) == expected_count,
                "Closure generator recipe layout differs: " + relative)
        require(len({row["name"] for row in replay["functions"]}) == expected_count,
                "Closure generator recipe function names are not unique: " + relative)
        for function in replay["functions"]:
            row = recovery_by_name.get(function["name"])
            require(row is not None and
                    (row["guest_vram"], row["rom_offset"], row["size"], row["rom_body_sha256"]) ==
                    (function["vram"], function["rom"], function["size"], function["rom_body_sha256"]),
                    "Closure generator function is not pinned in the full recovery manifest: " + function["name"])
            for table in function["jump_tables"]:
                key = (relative, function["name"], table["name"], table["rom"], table["vram"],
                       table["size"], table["sha256"])
                require(key in pinned_tables, "Closure jump table is absent from the reviewed table union: " + table["name"])
    symbols = symbol_identities(recipe.parent / lock["symbols"])
    names = set()
    for row in recoveries["functions"]:
        name, guest, offset, size = row["name"], row["guest_vram"], row["rom_offset"], row["size"]
        require(name not in names, "Duplicate ROM recovery function pin: " + name)
        names.add(name)
        require(symbols.get(name) == (guest, offset, size), "ROM recovery symbol identity differs: " + name)
        require(0 <= offset and 0 < size and offset + size <= len(rom), "Recovered function ROM bounds differ: " + name)
        require(digest(rom[offset:offset + size]) == row["rom_body_sha256"],
                "Recovered function ROM body hash differs: " + name)
        require(len(row["canonical_function_sha256"]) == 64,
                "Recovered canonical function digest is missing: " + name)
    require(len(names) == 545, "Expected all 545 reviewed ROM-closed function identities")
    return tables, recoveries, replay_recipes


def regenerate_closure_outputs(generator, rom_path, replay_recipes, work, logs):
    receipts = []
    output_root = work / "rom-closure-regeneration"
    output_root.mkdir(parents=True)
    for relative in sorted(replay_recipes):
        replay = replay_recipes[relative]
        folder = output_root / Path(relative).stem
        folder.mkdir()
        output = folder / "generated"
        symbols_path = folder / "symbols.toml"

        def section(function):
            return (f'[[section]]\nname="{function["name"]}"\nrom=0x{function["rom"]:X}\n' +
                    f'vram=0x{function["vram"]:X}\nsize=0x{function["size"]:X}\n' +
                    f'functions=[{{name="{function["name"]}",vram=0x{function["vram"]:X},size=0x{function["size"]:X}}}]\n')

        symbol_text = "".join(
            f'[[section]]\nname="{table["name"]}"\nrom=0x{table["rom"]:X}\nvram=0x{table["vram"]:X}\n' +
            f'size=0x{table["size"]:X}\nfunctions=[]\n'
            for function in replay["functions"] for table in function["jump_tables"])
        symbol_text += "".join(section(function) for function in replay["functions"] + replay["external_dependencies"])
        symbols_path.write_text(symbol_text, encoding="utf-8", newline="\n")
        config_path = folder / "generation.toml"
        config = ('[input]\nentrypoint=0x80000400\nsymbols_file_path=' + json.dumps(symbols_path.as_posix()) +
                  '\nrom_file_path=' + json.dumps(rom_path.as_posix()) + '\noutput_func_path=' +
                  json.dumps(output.as_posix()) + '\nfunctions_per_output_file=500\n[patches]\nignored=' +
                  json.dumps([function["name"] for function in replay["external_dependencies"]]) + '\n')
        config_path.write_text(config, encoding="utf-8", newline="\n")
        run([generator, config_path], logs / (Path(relative).stem + "-replay.log"), cwd=folder)
        generated = "\n".join(path.read_text(encoding="utf-8")
                                 for path in sorted(output.glob("funcs_*.c")))
        output_hash = digest(generated.encode())
        require(output_hash == replay["saved_generated_sha256"],
                "Source-built closure output differs from frozen pin: " + relative)
        functions, _ = split_functions(output)
        require(len(functions) == len(replay["functions"]) and
                set(functions) == {row["name"] for row in replay["functions"]},
                "Source-built closure callable inventory differs: " + relative)
        receipts.append({
            "source_configuration": relative,
            "source_configuration_sha256": replay["source_configuration_sha256"],
            "authoring_generator_sha256": replay["authoring_generator_sha256"],
            "source_built_generator_sha256": digest(Path(generator).read_bytes()),
            "function_count": len(functions),
            "saved_generated_sha256": replay["saved_generated_sha256"],
            "reconstructed_generated_sha256": output_hash,
            "aggregate_exact": True,
        })
    save(work / "closure-generator-replay.json", {
        "schema": "xr64.pc-closure-generator-replay.v1",
        "rom_sha256": digest(Path(rom_path).read_bytes()),
        "closures": receipts,
    })
    return receipts


def reconstruct(source, rom_path, work, cmake, generate_only=False, offline=False):
    source, rom_path, work = source.resolve(), rom_path.resolve(), work.resolve()
    source_manifest = verify_source_candidate(source)
    require(not work.exists() or not any(work.iterdir()), "Bootstrap work directory must be new/empty")
    require(not source.is_relative_to(work) and not work.is_relative_to(source), "Work and reviewed source must be separate")
    work.mkdir(parents=True, exist_ok=True)
    project = work / "project"
    project.mkdir()
    for relative in source_manifest["files"]:
        target = safe_path(project, relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / relative, target)
    shutil.copyfile(source / "source-manifest.json", project / "source-manifest.json")
    recipe = project / "reconstruction"
    lock = load(recipe / "reconstruction.lock.json")
    rom = rom_path.read_bytes()
    tables, recoveries, replay_recipes = verify_recipe_pins(recipe, lock, rom)
    alias = lock["resident_data_alias"]
    require(digest(rom[alias["rom"]:alias["rom"] + alias["size"]]) == alias["sha256"],
            "Resident data mapping differs")

    logs = work / "logs"
    generator_build = project / "build/generators"
    run([cmake, "-S", project / "toolchain/N64Recomp", "-B", generator_build,
         "-G", "Visual Studio 17 2022", "-A", "x64",
         "-T", lock["msvc_toolset"], "-DCMAKE_SYSTEM_VERSION=" + lock["windows_sdk"]],
        logs / "generator-configure.log")
    compiler_files = list(generator_build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"))
    require(len(compiler_files) == 1, "Ambiguous generator compiler identity")
    compiler = compiler_files[0].read_text()
    require('set(CMAKE_CXX_COMPILER_VERSION "' + lock["msvc_compiler_version"] + '")' in compiler,
            "Selected MSVC version differs from reviewed toolchain")
    run([cmake, "--build", generator_build, "--config", "Release", "--target",
         "N64RecompCLI", "RSPRecomp", "--parallel", "6"], logs / "generator-build.log")
    generators = {name: generator_build / "Release" / (name + ".exe") for name in ("N64Recomp", "RSPRecomp")}
    provenance = {
        "source_manifest_sha256": digest((source / "source-manifest.json").read_bytes()),
        "public_sources": load(recipe / "generator-provenance.json"),
        "generator_sources": {r: d["sha256"] for r, d in source_manifest["files"].items()
                              if r.startswith("toolchain/N64Recomp/")},
        "executables": {name: digest(path.read_bytes()) for name, path in generators.items()},
        "recovery_authoring_generator_sha256": recoveries["authoring_generator_sha256"],
        "recovery_recipe_source_built_generator_sha256": recoveries["source_built_generator_sha256"],
        "compiler": lock["msvc_compiler_version"], "toolset": lock["msvc_toolset"],
        "sdk": lock["windows_sdk"], "configuration": "Release",
        "policy": "Build from the exported source manifest; authoring-generator identity remains separately pinned, and source-built output must match every canonical digest."
    }
    save(work / "generator-build-provenance.json", provenance)
    closure_receipts = regenerate_closure_outputs(
        generators["N64Recomp"], rom_path, replay_recipes, work, logs)

    canonical = work / "canonical-guest"
    symbols = work / "generation.symbols.toml"
    original_symbols = (project / lock["symbols"]).read_text(encoding="utf-8")
    prefix = ""
    for i, table in enumerate(tables):
        prefix += ('[[section]]\nname="reconstruction_table_' + str(i) + '"\nrom=' + str(table["rom"]) +
                   "\nvram=" + str(table["vram"]) + "\nsize=" + str(table["size"]) + "\nfunctions=[]\n")
    prefix += ('[[section]]\nname="reconstruction_resident_data_alias"\nrom=' + str(alias["rom"]) +
               "\nvram=" + str(alias["vram"]) + "\nsize=" + str(alias["size"]) + "\nfunctions=[]\n\n")
    symbols.write_text(prefix + original_symbols, encoding="utf-8", newline="\n")
    config = (project / lock["configuration"]).read_text(encoding="utf-8")
    for key, value in (("symbols_file_path", symbols), ("rom_file_path", rom_path),
                       ("output_func_path", canonical)):
        config = re.sub(r"(?m)^" + key + r" = .*$", key + " = " + json.dumps(value.as_posix()), config)
    config_path = work / "generation.toml"
    config_path.write_text(config, encoding="utf-8", newline="\n")
    run([generators["N64Recomp"], config_path], logs / "guest-generation.log", cwd=work)
    functions, _ = split_functions(canonical)
    require(len(functions) == lock["canonical_function_count"], "Canonical guest function count differs")
    actual = {name: digest(body.encode()) for name, body in functions.items()}
    require(actual == load(recipe / "canonical-functions.json"), "Canonical generator output differs from reviewed recipe")
    for row in recoveries["functions"]:
        require(actual.get(row["name"]) == row["canonical_function_sha256"],
                "Recovered function generator output differs: " + row["name"])

    for name, edit in load(recipe / "guest-function-edits.json").items():
        functions[name] = apply_edit(functions[name], edit)
    output = project / "staging/rage-wars-recomp-spike/generated/ares_boot_to_combat"
    output.mkdir(parents=True)
    expected = load(recipe / "expected-guest-inputs.json")
    for filename, layout in load(recipe / "guest-layout.json").items():
        text = "".join(part["literal"] if "literal" in part else functions[part["function"]] for part in layout)
        write_candidate(output / filename, text, expected[filename])
    for filename, edit in load(recipe / "guest-metadata-edits.json").items():
        text = apply_edit((canonical / filename).read_text(encoding="utf-8"), edit)
        write_candidate(output / filename, text, expected[filename])
    run([sys.executable, project / "scripts/apply_modern_controls_hooks.py", "--rom", rom_path, "--apply"],
        logs / "existing-controls-hooks.log")
    run([sys.executable, project / "scripts/instrument_damage_reversal.py", "--rom", rom_path, "--generation-only"],
        logs / "existing-diagnostic-input.log")
    for filename, record in expected.items():
        text = (output / filename).read_text(encoding="utf-8")
        write_candidate(output / filename, text, record)
    verify_inventory(output, expected)
    require({p.name for p in output.iterdir()} == set(expected), "Reconstructed guest inventory differs")

    audio_manifest = load(project / "scripts/audio_rsp_manifest.json")
    verify_rom(rom, audio_manifest)
    audio = project / "build/audio-rsp-recovery"
    audio.mkdir(parents=True)
    audio_config = (
        "text_offset = " + str(audio_manifest["text_rom"]) + "\n" +
        "text_size = " + str(audio_manifest["text_size"]) + "\n" +
        "text_address = " + str(audio_manifest["text_rsp"]) + "\n" +
        "rom_file_path = " + json.dumps(rom_path.as_posix()) + "\n" +
        'output_file_path = "rage_wars_audio_ucode.cpp"\noutput_function_name = "rage_wars_audio_ucode"\n' +
        "extra_indirect_branch_targets = " + json.dumps(audio_manifest["handler_targets"]) + "\n")
    (audio / "audio.toml").write_text(audio_config, encoding="utf-8", newline="\n")
    run([generators["RSPRecomp"], audio / "audio.toml"], logs / "audio-generation.log", cwd=audio)
    audio_output = audio / "rage_wars_audio_ucode.cpp"
    require(digest(audio_output.read_bytes()) == lock["audio_output_sha256"], "Reconstructed audio differs")
    require("Unhandled instruction" not in (logs / "audio-generation.log").read_text(),
            "RSP generator reported an unhandled instruction")
    require(b"# INVALID" not in audio_output.read_bytes(), "Invalid audio instruction")
    inputs = {("staging/rage-wars-recomp-spike/generated/ares_boot_to_combat/" + name): record
              for name, record in expected.items()}
    inputs["build/audio-rsp-recovery/rage_wars_audio_ucode.cpp"] = {
        "sha256": digest(audio_output.read_bytes()), "bytes": audio_output.stat().st_size}
    result = {
        "schema": "xr64.pc-reconstruction-result.v2", "release_version": lock["release_version"],
        "source_manifest_sha256": provenance["source_manifest_sha256"],
        "rom_sha256": digest(rom), "canonical_function_count": len(functions),
        "recovered_rom_function_count": len(recoveries["functions"]), "rom_table_count": len(tables),
        "optional_snapshot_access": "not requested; path never resolved",
        "reconstructed_input_manifest": inputs, "candidate_input_comparison": "all exact raw bytes",
        "generator_provenance": "generator-build-provenance.json", "os_isolation": "open; no OS access barrier claimed",
        "runtime_acceptance": "pending on resulting executable", "build": "not requested" if generate_only else "pending"}
    save(work / "reconstruction-result.json", result)
    if generate_only:
        print("PASS: all", len(inputs), "candidate guest/audio inputs reconstructed with exact pinned bytes.")
        return result
    command = ["powershell", "-NoProfile", "-File", project / "scripts/build_pc_candidate.ps1",
               "-BuildDir", project / "build/reconstructed-pc", "-CMake", cmake,
               "-Toolset", lock["msvc_toolset"], "-WindowsSDK", lock["windows_sdk"]]
    if offline:
        command.append("-Offline")
    run(command, logs / "candidate-build-and-contracts.log")
    build = project / "build/reconstructed-pc"
    result["build"] = "passed"
    result["executable_sha256"] = digest((build / "Release/RageWarsRecompiled.exe").read_bytes())
    result["package_manifest"] = load(build / "package-stage/package-manifest.json")
    result["input_audit"] = load(build / "dependency-input-audit.json")
    save(work / "reconstruction-result.json", result)
    print("PASS build/contracts/package; reconstructed executable SHA256:", result["executable_sha256"])
    print("Isolation and exact-executable runtime gates remain open.")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-candidate", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generate-only", action="store_true")
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    reconstruct(args.source_candidate, args.rom, args.work, args.cmake, args.generate_only, args.offline)


if __name__ == "__main__":
    main()
