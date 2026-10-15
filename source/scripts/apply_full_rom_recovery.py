#!/usr/bin/env python3
"""Reproduce the October 13-14, 2026 whole-ROM function recovery layer.

The accepted base (v2 recipe + bounded recovery of October 6) covered only code reached by an
execution trace. This layer adds original-ROM functions the trace never reached (multiplayer and
split-screen paths among them) without replacing any existing body. Two ROM-only generation stages:
  stage 1: 691 functions placed from a size/order reference, generator helpers, switch tables;
  stage 2: 65 functions carved from the remaining uncovered code gaps.
Everything is regenerated from the external ROM with the qualified generator; each generated body,
each ROM function window and every postimage is pinned in scripts/full_rom_recovery_20261014.json.
No generated guest code is stored in the recipe.
"""
import argparse, glob, hashlib, json, re, subprocess, sys, tomllib
from pathlib import Path
sys.dont_write_bytecode = True
from reconstruction_common import digest, load, require, save

ROOT = Path(__file__).resolve().parents[1]
SP = "staging/rage-wars-recomp-spike"
M1, M2 = "FULL ROM RECOVERY 20261013", "GAP ROM RECOVERY 20261013"
BOUNDED_END = "// END BOUNDED ROM RECOVERY 20261006"
SHIM_NAMES = ("full_rom_func_0020238C", "full_rom_func_002023A8", "full_rom_func_002023DC")
SHIM_TEXT = ("// Host shims: the original bodies are cache maintenance loops (cache instruction) with no guest-visible effect.\n" +
             "".join("RECOMP_FUNC void %s(uint8_t* rdram, recomp_context* ctx) { (void)rdram; (void)ctx; }\n" % n for n in SHIM_NAMES))
CHUNK = re.compile(r"(?m)^(?=RECOMP_FUNC void )")
GEN_ALL_ORDER = lambda folder: sorted(glob.glob(str(folder / "funcs_*.c")))


def newline_of(text):
    return "\r\n" if "\r\n" in text else "\n"


def func_section_s1(r):
    return ('[[section]]\nname="%s"\nrom=0x%X\nvram=0x%X\nsize=0x%X\nfunctions=[{name="%s",vram=0x%X,size=0x%X}]\n'
            % (r["name"], r["rom"], r["vram"], r["size"], r["name"], r["vram"], r["size"]))


def table_section(t):
    return '[[section]]\nname = "%s"\nrom = 0x%X\nvram = 0x%X\nsize = 0x%X\nfunctions = [\n]\n\n' % (t["name"], t["rom"], t["vram"], t["size"])


def func_section_s2(r):
    return ('[[section]]\nname = "%s"\nrom = 0x%X\nvram = 0x%X\nsize = 0x%X\nfunctions = [\n    { name = "%s", vram = 0x%X, size = 0x%X },\n]\n\n'
            % (r["name"], r["rom"], r["vram"], r["size"], r["name"], r["vram"], r["size"]))


def stage1_symbol_block(spec):
    s1 = spec["stage1"]; kept = set(s1["kept"])
    sections = [func_section_s1(r) for r in s1["candidates"] if r["name"] in kept]
    tables = "".join(table_section(t) for t in s1["tables"])
    return ("# BEGIN " + M1 + "\n" + "\n".join(sections) + "\n# FULL ROM JUMP TABLES 20261013\n" + tables).rstrip() + "\n# END " + M1 + "\n"


def stage2_symbol_block(spec):
    s2 = spec["stage2"]; ignored = set(s2["ignored_generation"])
    sections = [func_section_s2(r) for r in s2["candidates"] if r["name"] not in ignored]
    return ("# BEGIN " + M2 + "\n" + "".join(sections) + "# END " + M2 + "\n\n# FULL ROM JUMP TABLES 20261013\n").rstrip() + "\n"


def fix_symbols(text, spec):
    c = spec["symbol_correction"]
    if c["after"] in text:
        return text
    require(text.count(c["before"]) == 1, "Table symbol preimage differs")
    return text.replace(c["before"], c["after"], 1)


def baseline_symbol_text(text, spec):
    """Undo this layer on the shipped (final) symbol text, returning the bounded-recovery form."""
    marker = "# BEGIN " + M1
    if marker in text:
        require(text.count(marker) == 1 and text.count("# BEGIN " + M2) == 1, "Ambiguous full-ROM recovery symbol blocks")
        text = text[:text.index(marker)].rstrip() + "\n"
    c = spec["symbol_correction"]
    if c["before"] not in text and c["after"] in text:
        require(text.count(c["after"]) == 1, "Ambiguous corrected table symbol")
        text = text.replace(c["after"], c["before"], 1)
    require(digest(text.encode()) == spec["base_symbol_sha256"], "Base symbol bytes differ")
    return text


def generation_symbols(base_text, spec, additions):
    return fix_symbols(base_text, spec).rstrip() + "\n\n" + additions


def run_generator(spec, rom_path, generator, work, name, symbols_text, extra_ignored, config_text):
    stage = work / name
    stage.mkdir(parents=True, exist_ok=True)
    out = stage / "generated"
    require(not out.exists(), "Generation output must be fresh")
    (stage / "symbols.toml").write_text(symbols_text, encoding="utf-8", newline="\n")
    patches = config_text[config_text.index("[patches]"):]
    require(patches.count("ignored = [") == 1, "Config ignored list differs")
    patches = patches.replace("ignored = [", "ignored = [\n" + "".join('    "%s",\n' % n for n in extra_ignored), 1)
    head = ('[input]\nentrypoint = 0x80000400\nsymbols_file_path = %s\nrom_file_path = %s\noutput_func_path = %s\nfunctions_per_output_file = 500\n\n'
            % (json.dumps((stage / "symbols.toml").as_posix()), json.dumps(rom_path.resolve().as_posix()), json.dumps(out.as_posix())))
    (stage / "config.toml").write_text(head + patches, encoding="utf-8", newline="\n")
    run = subprocess.run([str(generator.resolve()), str((stage / "config.toml").resolve())], capture_output=True, text=True, cwd=stage)
    (stage / "generator.log").write_text(run.stdout + run.stderr, encoding="utf-8")
    require(run.returncode == 0 and "Error recompiling" not in run.stdout + run.stderr, "Generator failed; inspect generator.log")
    return out


def joined_defs(folder):
    text = "".join(Path(p).read_text(encoding="utf-8", errors="ignore") for p in GEN_ALL_ORDER(folder))
    defs = {}
    for chunk in CHUNK.split(text):
        m = re.match(r"RECOMP_FUNC void (\w+)\(", chunk)
        if m:
            require(m.group(1) not in defs, "Duplicate generated definition")
            defs[m.group(1)] = chunk
    return defs


def per_file_chunks(folder, prefix):
    bodies = []
    for p in GEN_ALL_ORDER(folder):
        for chunk in CHUNK.split(Path(p).read_text(encoding="utf-8", errors="ignore")):
            if re.match(r"RECOMP_FUNC void (" + prefix + r"\w+)\(", chunk):
                bodies.append(chunk)
    return bodies


def verify_rom_windows(rom, rows, label):
    for r in rows:
        require(digest(rom[r["rom"]:r["rom"] + r["size"]]) == r["sha256"], label + " ROM window differs: " + r["name"])


def generate_stage1(spec, rom, rom_path, generator, work, base_symbols, config):
    s1 = spec["stage1"]
    verify_rom_windows(rom, [r for r in s1["candidates"] if r["name"] in set(s1["kept"])], "Stage 1")
    verify_rom_windows(rom, s1["tables"], "Stage 1 table")
    add = "".join(func_section_s1(r) for r in s1["candidates"]) + "# FULL ROM JUMP TABLES 20261013\n" + "".join(table_section(t) for t in s1["tables"])
    out = run_generator(spec, rom_path, generator, work, "stage1", generation_symbols(base_symbols, spec, add), s1["ignored_generation"], config)
    defs = joined_defs(out)
    additive = "\n".join(per_file_chunks(out, "full_rom_func_"))
    body_all = CHUNK.split(additive[additive.index("RECOMP_FUNC void"):])
    keep = [b for b in body_all if (m := re.match(r"RECOMP_FUNC void (full_rom_func_\w+)\(", b)) and m.group(1) in set(s1["kept"])]
    names = [re.match(r"RECOMP_FUNC void (\w+)\(", b).group(1) for b in keep]
    require(names == s1["kept"], "Stage 1 generated function set/order differs")
    for n, b in zip(names, keep):
        require(digest(b.encode()) == s1["body_sha256"][n], "Stage 1 generated body differs: " + n)
    extra = []
    for n in s1["helpers"]:
        require(n in defs and digest(defs[n].encode()) == s1["helper_sha256"][n], "Stage 1 helper body differs: " + n)
        extra.append(defs[n])
    return names, keep, extra


def generate_stage2(spec, rom, rom_path, generator, work, stage1_symbols, config):
    s2 = spec["stage2"]
    verify_rom_windows(rom, s2["candidates"], "Stage 2")
    add = stage2_symbol_block_all(spec)
    out = run_generator(spec, rom_path, generator, work, "stage2", stage1_symbols.rstrip() + "\n\n" + add, s2["ignored_generation"], config)
    defs = joined_defs(out)
    ignored = set(s2["ignored_generation"])
    names = [n for n in defs if n.startswith("gap_rom_func_") and n not in ignored]
    require(names == s2["kept"], "Stage 2 generated function set/order differs")
    for n in names:
        require(digest(defs[n].encode()) == s2["body_sha256"][n], "Stage 2 generated body differs: " + n)
    return names, [defs[n] for n in names]


def stage2_symbol_block_all(spec):
    return "# BEGIN GAP RECOVERY 20261013\n" + "".join(func_section_s2(r) for r in spec["stage2"]["candidates"]) + "# END GAP RECOVERY 20261013\n"


def rewrite(texts, name, fn):
    t = texts[name]
    texts[name] = fn(t, newline_of(t))


def append_stage(texts, symbols, spec, marker, names, bodies, extra_bodies, extra_names, shims, start_index, info, anchors):
    def funcs54(t, e):
        block = "// BEGIN " + marker + "\n" + "".join(bodies + extra_bodies).rstrip() + "\n" + shims + "// END " + marker + "\n"
        return t.rstrip("\r\n") + e + block.replace("\n", e)
    rewrite(texts, "funcs_54.c", funcs54)
    protos = "".join("void %s(uint8_t*,recomp_context*);\n" % n for n in names + extra_names)
    if shims:
        protos += "".join("void %s(uint8_t*,recomp_context*);\n" % n for n in SHIM_NAMES)
    def header(t, e):
        k = t.index("#ifdef __cplusplus" + e + "}")
        return t[:k] + ("// BEGIN " + marker + "\n" + protos + "// END " + marker + "\n").replace("\n", e) + t[k:]
    rewrite(texts, "funcs.h", header)
    def overlays(t, e):
        decl, ent = [], []
        for i, n in enumerate(names):
            v, sz, rr = info[n]; idx = start_index + i; sec = "section_%d_%s_funcs" % (idx, n)
            decl.append("static FuncEntry %s[] = {{ .func = %s, .offset = 0, .rom_size = 0x%X }};" % (sec, n, sz))
            ent.append("{ .rom_addr = 0x%08X, .ram_addr = 0x%08X, .size = 0x%X, .funcs = %s, .num_funcs = 1, .relocs = nullptr, .num_relocs = 0, .index = %d }," % (rr, v, sz, sec, idx))
        m1 = anchors[0] + e; m2 = anchors[1] + e
        require(t.count(m1) == 1 and t.count(m2) == 1, "Overlay anchors differ")
        t = t.replace(m1, m1 + "// BEGIN " + marker + e + e.join(decl) + e + "// END " + marker + e, 1)
        t = t.replace(m2, m2 + "// BEGIN " + marker + " ENTRIES" + e + e.join(ent) + e + "// END " + marker + " ENTRIES" + e, 1)
        cur = re.search(r"const size_t num_sections = (\d+);", t)
        require(int(cur.group(1)) == start_index, "Section count differs")
        return t.replace(cur.group(0), "const size_t num_sections = %d;" % (start_index + len(names)), 1)
    rewrite(texts, "recomp_overlays.inl", overlays)


def transform(repo, spec, bodies1, bodies2, base_symbols):
    folder = repo / SP / "generated/ares_boot_to_combat"
    raw = {n: (folder / n).read_bytes() for n in spec["base_inputs"]}
    for n, r in spec["base_inputs"].items():
        require(digest(raw[n]) == r["sha256"], "Recovery requires the exact base guest-input bytes: " + n)
    texts = {n: d.decode("utf-8") for n, d in raw.items()}
    s1, s2 = spec["stage1"], spec["stage2"]
    names1, keep1, extra1 = bodies1
    info1 = {r["name"]: (r["vram"], r["size"], r["rom"]) for r in s1["candidates"]}
    append_stage(texts, None, spec, M1, names1, keep1, extra1, s1["helpers"], SHIM_TEXT, spec["base_num_sections"], info1,
                 (BOUNDED_END, BOUNDED_END + " ENTRIES"))
    names2, keep2 = bodies2
    info2 = {r["name"]: (r["vram"], r["size"], r["rom"]) for r in s2["candidates"]}
    append_stage(texts, None, spec, M2, names2, keep2, [], [], "", spec["base_num_sections"] + len(names1), info2,
                 ("// END " + M1, "// END " + M1 + " ENTRIES"))
    symbols = fix_symbols(base_symbols, spec).rstrip() + "\n\n" + stage1_symbol_block(spec)
    symbols = symbols.rstrip() + "\n\n" + stage2_symbol_block(spec)
    outputs = {}
    for n, text in texts.items():
        outputs[n] = raw[n] if text == raw[n].decode("utf-8") else text.encode("utf-8")
    return outputs, symbols.encode("utf-8")


def apply_layer(repo, spec, rom_path, generator, work, apply=False, author_result=False, qualification=None):
    rom = rom_path.read_bytes()
    require(len(rom) == spec["rom_bytes"] and digest(rom) == spec["rom_sha256"], "ROM identity mismatch")
    gen_hash = digest(generator.read_bytes())
    if gen_hash != spec["qualified_generator_sha256"]:
        require(qualification is not None and qualification.get("generator_sha256") == gen_hash and
                qualification.get("rom_sha256") == spec["rom_sha256"] and
                qualification.get("canonical_functions_verified") == 3751 and qualification.get("source_manifest_sha256"),
                "Generator needs exact base canonical/source qualification")
    sym_path = repo / SP / "symbols/rage_wars.us.ares_boot_to_combat.toml"
    base_symbols = sym_path.read_text(encoding="utf-8")
    require(digest(base_symbols.encode()) == spec["base_symbol_sha256"], "Base symbol bytes differ")
    config = (repo / SP / "configs/rage_wars.us.ares_boot_to_combat.toml").read_text(encoding="utf-8")
    work.mkdir(parents=True, exist_ok=True)
    bodies1 = generate_stage1(spec, rom, rom_path, generator, work, base_symbols, config)
    stage1_symbols = fix_symbols(base_symbols, spec).rstrip() + "\n\n" + stage1_symbol_block(spec)
    bodies2 = generate_stage2(spec, rom, rom_path, generator, work, stage1_symbols, config)
    outputs, symbols = transform(repo, spec, bodies1, bodies2, base_symbols)
    result = {"inputs": {n: {"sha256": digest(d), "bytes": len(d)} for n, d in outputs.items()}, "symbol_sha256": digest(symbols)}
    if author_result:
        save(work / "authored-result.json", result)
        return result
    expected_changed = {n: spec["expected_result"]["inputs"][n] for n in result["inputs"]}
    require(result["inputs"] == expected_changed and result["symbol_sha256"] == spec["expected_result"]["symbol_sha256"],
            "Recovery postimage differs from reviewed recipe")
    if apply:
        folder = repo / SP / "generated/ares_boot_to_combat"
        for n, d in outputs.items():
            if (folder / n).read_bytes() != d:
                (folder / n).write_bytes(d)
        sym_path.write_bytes(symbols)
    receipt = {"schema": "xr64.full-rom-recovery-replay.v1", "rom_sha256": spec["rom_sha256"], "generator_sha256": gen_hash,
               "authoring_generator_sha256": spec["qualified_generator_sha256"], "source_built_qualification": qualification,
               "stage1_functions": len(bodies1[0]), "stage1_helpers": len(spec["stage1"]["helpers"]), "stage1_tables": len(spec["stage1"]["tables"]),
               "stage2_functions": len(bodies2[0]), "expected_result": result, "applied": apply,
               "runtime_acceptance": "owner-reported multiplayer on the staged executable only; not established by generation"}
    save(work / "receipt.json", receipt)
    return receipt


def verify_installed(repo, spec):
    folder = repo / SP / "generated/ares_boot_to_combat"
    for n, r in spec["expected_result"]["inputs"].items():
        require(digest((folder / n).read_bytes()) == r["sha256"], "Installed input differs: " + n)
    require(digest((repo / SP / "symbols/rage_wars.us.ares_boot_to_combat.toml").read_bytes()) == spec["expected_result"]["symbol_sha256"],
            "Installed symbols differ")
    text = "".join(Path(p).read_text(encoding="utf-8", errors="ignore") for p in GEN_ALL_ORDER(folder))
    count = len(re.findall(r"(?m)^RECOMP_FUNC void \w+\(", text))
    require(count == spec["expected_definition_count"], "Definition count differs")
    return {"installed_exact": True, "function_count": count}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--repo", type=Path, default=ROOT)
    p.add_argument("--spec", type=Path, default=ROOT / "scripts/full_rom_recovery_20261014.json")
    p.add_argument("--rom", type=Path); p.add_argument("--generator", type=Path); p.add_argument("--work", type=Path)
    p.add_argument("--apply", action="store_true"); p.add_argument("--verify", action="store_true"); p.add_argument("--author-result", action="store_true")
    a = p.parse_args(); spec = load(a.spec)
    require(spec["schema"] == "xr64.full-rom-recovery.v1", "Wrong recovery schema")
    if a.verify:
        result = verify_installed(a.repo.resolve(), spec)
    else:
        require(a.rom and a.generator and a.work, "ROM, generator and fresh work are required")
        result = apply_layer(a.repo.resolve(), spec, a.rom, a.generator, a.work.resolve(), a.apply, a.author_result)
    print(json.dumps({k: v for k, v in result.items() if k != "expected_result"}))


if __name__ == "__main__":
    main()
