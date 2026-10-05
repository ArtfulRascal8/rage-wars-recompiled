#!/usr/bin/env python3
"""Author a reviewed integration recipe from clean generation and pinned ROM closures.
This private maintenance command reads accepted working outputs; the bootstrap does not.
"""
import argparse
import difflib
import json
import shutil
import tomllib
from pathlib import Path
from reconstruction_common import digest, load, require, save, split_functions, make_edit

ROM_SHA256 = "0433043aaba2649bdd1fe717c4020550ac663c0362527aa082490f5977a3e46b"
AUTHORING_GENERATOR_SHA256 = "b47af74141c71b2aad45088254865b4ed31b021bad19dd45a0130bf8690f1ca2"
RECOVERY_SPECS = {
    "scripts/gameplay_menu_rom_closure_20261003.json": {
        "sha256": "9bb65b0430bd3e33d39aff2f319847b5bb3ba74687dbb1d1a7b341490f28636c",
        "functions": 496,
    },
    "scripts/targeted_jal_rom_closure_20261003.json": {
        "sha256": "749000e4cc51d9c33afbf4f5fd4ff109b0b47509fd4e33e6cce00c48087ddaf3",
        "functions": 46,
    },
    "scripts/targeted_nonreturn_worker_rom_closure_20261003.json": {
        "sha256": "b2832e7450f1c036c6f336bab2f4f725ced750e697538c1787601033cdf25ea4",
        "functions": 3,
    },
}


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


def copy_reviewed_recipe(base, output):
    for path in sorted(base.iterdir()):
        require(path.is_file() and not path.is_symlink(), "Base recipe entry must be a regular file")
        require(path.suffix in {".json", ".patch", ".md"}, "Unexpected base recipe payload")
        path.read_text(encoding="utf-8")
        shutil.copyfile(path, output / path.name)


def refresh_generator_provenance(repo, base_recipe, output):
    upstream = repo / "build/source-reconstruction-20261002/upstream/N64Recomp-ffb39cdad1da5de07eaaa48bd1db4a89a7986771"
    modified = repo / "toolchain/N64Recomp"
    names = ("include/recomp.h", "src/analysis.cpp", "src/cgenerator.cpp")
    patch = []
    for name in names:
        before = (upstream / name).read_text(encoding="utf-8").splitlines(keepends=True)
        after = (modified / name).read_text(encoding="utf-8").splitlines(keepends=True)
        patch.extend(difflib.unified_diff(before, after, fromfile="upstream/" + name,
                                          tofile="modified/" + name))
    patch_text = "".join(patch)
    (output / "generator-local.patch").write_text(patch_text, encoding="utf-8", newline="\n")
    provenance = load(base_recipe / "generator-provenance.json")
    provenance["local_patch_sha256"] = digest(patch_text.encode())
    provenance["current_working_source_note"] = (
        "The public-source diff includes the accepted context-aware LOOKUP_FUNC forwarder and jump-table/GPR output fixes, "
        "plus the two recovered-entry/journal helper declarations currently present in recomp.h. PUBLIC_RELEASE source "
        "preparation removes journal/replay call behavior; the exported source-manifest and clean generator-build "
        "provenance pin the exact current bytes.")
    provenance["current_header_additions"] = [
        "xr64_rage_wars_note_recovered_rom_entry",
        "xr64_rage_wars_invoke_function_with_context_trace",
    ]
    save(output / "generator-provenance.json", provenance)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--canonical", type=Path, required=True)
    parser.add_argument("--base-recipe", type=Path, required=True)
    parser.add_argument("--rom-tables", type=Path, required=True)
    parser.add_argument("--table-provenance", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--closure", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    output = args.output.resolve()
    base_recipe = args.base_recipe.resolve()
    require(not output.exists(), "Recipe destination must be new")
    require(repo.is_dir() and base_recipe.is_dir(), "Repository/base recipe is missing")
    require(args.generator.is_file(), "Source-built generator is missing")
    require(not output.is_relative_to(repo / "staging/rage-wars-recomp-spike/generated"),
            "Recipe output cannot be inside generated guest inputs")

    rom = args.rom.resolve().read_bytes()
    require(digest(rom) == ROM_SHA256, "Owner ROM identity mismatch")
    lock = load(base_recipe / "reconstruction.lock.json")
    require(lock["schema"] == "xr64.pc-reconstruction.v1", "Base recipe must be the preserved v1 recipe")
    require(lock["rom_sha256"] == digest(rom), "Base recipe ROM identity differs")
    old_canonical = load(base_recipe / "canonical-functions.json")

    canonical, _ = split_functions(args.canonical.resolve())
    current = repo / "staging/rage-wars-recomp-spike/generated/ares_boot_to_combat"
    candidate, layouts = split_functions(current)
    require(len(canonical) == 3751, "Expected 3,751 canonical guest functions")
    require(len(candidate) == 3751, "Expected 3,751 accepted guest functions")
    require(set(canonical) == set(candidate), "Canonical/accepted callable inventories differ")
    canonical_hashes = {name: digest(body.encode()) for name, body in sorted(canonical.items())}
    require(len(old_canonical) == 3206, "Preserved v1 canonical inventory differs")
    require(all(canonical_hashes.get(name) == sha for name, sha in old_canonical.items()),
            "One of the original 3,206 canonical function pins changed")

    closure_by_path = {}
    records = []
    for raw_path in args.closure:
        path = raw_path.resolve()
        require(path.is_relative_to(repo), "Closure spec must be inside the integration repository")
        relative = path.relative_to(repo).as_posix()
        require(relative in RECOVERY_SPECS, "Unexpected ROM closure spec: " + relative)
        raw = path.read_bytes()
        expected = RECOVERY_SPECS[relative]
        require(digest(raw) == expected["sha256"], "Frozen ROM closure spec changed: " + relative)
        spec = json.loads(raw)
        require(spec.get("rom_sha256") == digest(rom), "ROM closure owner-ROM identity differs: " + relative)
        require(spec.get("generator_sha256") == AUTHORING_GENERATOR_SHA256,
                "Original ROM closure authoring-generator pin differs: " + relative)
        require(len(spec.get("functions", [])) == expected["functions"],
                "Frozen ROM closure function count differs: " + relative)
        require(relative not in closure_by_path, "Duplicate closure spec: " + relative)
        closure_by_path[relative] = (spec, raw)
    require(set(closure_by_path) == set(RECOVERY_SPECS), "All three frozen closure specs are required")

    identities = symbol_identities(repo / lock["symbols"])
    seen_names = set()
    closure_records = []
    closure_generation_recipes = []
    for relative in sorted(closure_by_path):
        spec, raw = closure_by_path[relative]
        source_hash = digest(raw)
        independent = [row["sha256"] for row in spec.get("independent_ghidra_proofs", [])]
        closure_records.append({
            "source_configuration": relative,
            "source_configuration_sha256": source_hash,
            "authoring_generator_sha256": spec["generator_sha256"],
            "saved_generated_sha256": spec["generated_sha256"],
            "function_count": len(spec["functions"]),
            "independent_ghidra_proof_sha256": independent,
        })
        closure_generation_recipes.append({
            "source_configuration": relative,
            "source_configuration_sha256": source_hash,
            "authoring_generator_sha256": spec["generator_sha256"],
            "saved_generated_sha256": spec["generated_sha256"],
            "functions_per_output_file": 500,
            "functions": [{
                "name": function["name"], "vram": function["vram"],
                "rom": function["rom"], "size": function["size"],
                "rom_body_sha256": function["sha256"],
                "jump_tables": [{key: table[key] for key in ("name", "rom", "vram", "size", "sha256")}
                                for table in function.get("jump_tables", [])],
            } for function in spec["functions"]],
            "external_dependencies": [{key: function[key] for key in ("name", "vram", "rom", "size")}
                                       for function in spec["external_dependencies"]],
        })
        for function in spec["functions"]:
            name = function["name"]
            require(name not in seen_names, "Duplicate ROM recovery function: " + name)
            seen_names.add(name)
            vram, rom_offset, size = function["vram"], function["rom"], function["size"]
            require(identities.get(name) == (vram, rom_offset, size),
                    "Recovered function symbol/ROM mapping differs: " + name)
            require(0 <= rom_offset and 0 < size and rom_offset + size <= len(rom),
                    "Recovered function ROM bounds differ: " + name)
            require(digest(rom[rom_offset:rom_offset + size]) == function["sha256"],
                    "Recovered function original-ROM hash differs: " + name)
            require(name in canonical and name in candidate, "Recovered function missing from 3,751 inputs: " + name)
            records.append({
                "name": name,
                "guest_vram": vram,
                "rom_offset": rom_offset,
                "size": size,
                "rom_body_sha256": function["sha256"],
                "canonical_function_sha256": canonical_hashes[name],
                "source_configuration": relative,
                "source_configuration_sha256": source_hash,
            })
    require(len(records) == 545, "Expected 545 functions covered by the frozen ROM closure set")

    rom_tables = load(args.rom_tables)
    table_provenance = load(args.table_provenance)
    legacy_tables = load(base_recipe / "rom-tables.json")
    require(len(legacy_tables) == 29 and rom_tables[:29] == legacy_tables,
            "The original 29 ROM table pins must be byte-for-byte retained")
    require(len(rom_tables) == 60 and table_provenance["table_pin_total"] == 60,
            "Expected the 60-entry reviewed ROM table union")
    for row in rom_tables:
        start, size = row["rom"], row["size"]
        require(0 <= start and 0 < size and start + size <= len(rom), "ROM table bounds differ: " + row["name"])
        require(digest(rom[start:start + size]) == row["sha256"], "ROM table identity differs: " + row["name"])
    require(table_provenance["rom_sha256"] == digest(rom), "ROM table provenance identity differs")

    # Generate the explicit exact-input recipe from the 3,751-function source set.
    hooks = load(repo / "scripts/modern_controls_hooks.json")
    edited = dict(candidate)
    for row in hooks:
        body = edited[row["symbol"]]
        anchored = row["code"] + row["anchor"] if row["before"] else row["anchor"] + row["code"]
        require(body.count(anchored) == 1, "Existing controls hook differs: " + row["symbol"])
        edited[row["symbol"]] = body.replace(anchored, row["anchor"], 1)

    edits, review = {}, []
    for name in sorted(candidate):
        if canonical[name] == edited[name]:
            continue
        edits[name] = make_edit(canonical[name], edited[name])
        review.extend(difflib.unified_diff(canonical[name].splitlines(True), edited[name].splitlines(True),
                      fromfile="canonical/" + name, tofile="accepted/" + name, n=3))
    metadata = {}
    for name in ("funcs.h", "recomp_overlays.inl", "lookup.cpp"):
        old = (args.canonical.resolve() / name).read_text(encoding="utf-8")
        new = (current / name).read_text(encoding="utf-8")
        metadata[name] = make_edit(old, new)
        review.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                      fromfile="canonical/" + name, tofile="accepted/" + name, n=3))

    for path in sorted(base_recipe.iterdir()):
        require(path.is_file() and not path.is_symlink(), "Base recipe entry must be a regular file")
        require(path.suffix in {".json", ".patch", ".md"}, "Unexpected base recipe payload")
        path.read_text(encoding="utf-8")
    output.mkdir(parents=True)
    copy_reviewed_recipe(base_recipe, output)
    refresh_generator_provenance(repo, base_recipe, output)
    shutil.copyfile(args.rom_tables.resolve(), output / "rom-tables.json")
    shutil.copyfile(args.table_provenance.resolve(), output / "rom-table-provenance.json")
    evidence_dir = repo / "build/release-v0.2.1-beta.1-20261003/source-reconstruction"
    for evidence_name in ("closure-generator-replay.json", "recovery496-full-generator-differences.json",
                          "recovery496-full-generator-differences.md"):
        evidence_path = evidence_dir / evidence_name
        require(evidence_path.is_file(), "Reviewed closure evidence missing: " + evidence_name)
        shutil.copyfile(evidence_path, output / evidence_name)
    save(output / "canonical-functions.json", canonical_hashes)
    save(output / "rom-function-recoveries.json", {
        "schema": "xr64.pc-rom-function-recoveries.v1",
        "rom_sha256": digest(rom),
        "authoring_generator_sha256": AUTHORING_GENERATOR_SHA256,
        "source_built_generator_sha256": digest(args.generator.resolve().read_bytes()),
        "source_closures": closure_records,
        "closure_generation_recipes": closure_generation_recipes,
        "function_count": len(records),
        "functions": sorted(records, key=lambda row: (row["rom_offset"], row["guest_vram"])),
        "validation": "All 545 function identities, source-ROM byte hashes, current symbol mappings and canonical generated-function hashes are pinned; ROM bytes are not embedded.",
    })
    save(output / "guest-layout.json", layouts)
    save(output / "guest-function-edits.json", edits)
    save(output / "guest-metadata-edits.json", metadata)

    outputs = {}
    for path in sorted(current.iterdir()):
        if not path.is_file():
            continue
        raw = path.read_bytes()
        require(b"\r" not in raw.replace(b"\r\n", b""), "Unsupported lone CR in " + path.name)
        require(not (b"\r\n" in raw and b"\n" in raw.replace(b"\r\n", b"")),
                "Mixed newlines need an explicit recipe: " + path.name)
        outputs[path.name] = {"sha256": digest(raw), "bytes": len(raw),
                              "newline": "crlf" if b"\r\n" in raw else "lf"}
    save(output / "expected-guest-inputs.json", outputs)
    (output / "integration.patch").write_text("".join(review), encoding="utf-8", newline="\n")

    new_lock = dict(lock)
    new_lock.update({
        "schema": "xr64.pc-reconstruction.v2",
        "release_version": "0.2.1-beta.1",
        "canonical_function_count": len(canonical),
        "recovered_rom_function_count": len(records),
        "rom_table_count": len(rom_tables),
        "canonical_function_manifest_sha256": digest((output / "canonical-functions.json").read_bytes()),
        "rom_function_recoveries_sha256": digest((output / "rom-function-recoveries.json").read_bytes()),
        "rom_tables_sha256": digest((output / "rom-tables.json").read_bytes()),
        "rom_table_provenance_sha256": digest((output / "rom-table-provenance.json").read_bytes()),
        "generator_local_patch_sha256": digest((output / "generator-local.patch").read_bytes()),
        "generator_provenance_sha256": digest((output / "generator-provenance.json").read_bytes()),
        "closure_generator_replay_sha256": digest((output / "closure-generator-replay.json").read_bytes()),
        "recovery496_difference_json_sha256": digest((output / "recovery496-full-generator-differences.json").read_bytes()),
        "recovery496_difference_markdown_sha256": digest((output / "recovery496-full-generator-differences.md").read_bytes()),
        "preserved_v1_canonical_function_count": len(old_canonical),
        "preserved_v1_recipe_lock_sha256": digest((base_recipe / "reconstruction.lock.json").read_bytes()),
        "publication": "not cleared",
    })
    save(output / "reconstruction.lock.json", new_lock)
    save(output / "integration-review.json", {
        "canonical_callables": len(canonical),
        "accepted_callables": len(candidate),
        "preserved_v1_canonical_callables": len(old_canonical),
        "function_edits": len(edits),
        "prior_v1_function_edits": len(load(base_recipe / "guest-function-edits.json")),
        "added_function_edits_since_v1": len(edits) - len(load(base_recipe / "guest-function-edits.json")),
        "rom_recovery_functions": len(records),
        "existing_controls_hooks_applied_separately": len(hooks),
        "layout_literal_bytes": sum(len(part.get("literal", "").encode()) for row in layouts.values() for part in row),
        "metadata_edits": list(metadata),
        "rom_table_count": len(rom_tables),
        "policy": "Exact callable inventories, old canonical digests, original-ROM extents/hashes, explicit patch preimages/results, and final raw-file hashes; no semantic-equivalence waiver.",
        "review_note": "The 18 additional differences are individually listed with ROM/function identities and exact canonical-versus-accepted call-target diffs in recovery496-full-generator-differences.json.",
    })
    print("Authored reviewed private v2 recipe for", len(canonical), "canonical functions,",
          len(records), "ROM-closed functions,", len(edits), "explicit edits and", len(rom_tables), "table pins.")


if __name__ == "__main__":
    main()
