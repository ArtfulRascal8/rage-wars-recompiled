#!/usr/bin/env python3
"""Reproduce the admitted October 6 ROM bodies, metadata and actual direct bindings.
The v2 base reconstruction pins remain intact; this layer verifies its exact
preimages, regenerates new bodies from the external ROM, and checks postimages.
"""
import argparse, hashlib, json, re, struct, subprocess, sys
from pathlib import Path
sys.dont_write_bytecode = True
from reconstruction_common import digest, load, require, save
from prepare_demo_source import mask, end_pair

ROOT = Path(__file__).resolve().parents[1]
SP = "staging/rage-wars-recomp-spike"
BEGIN = "// BEGIN BOUNDED ROM RECOVERY 20261006"
END = "// END BOUNDED ROM RECOVERY 20261006"


def function_body(text, name):
    match = re.search(r"RECOMP_FUNC void " + re.escape(name) + r"\([^\n]*\) \{", text)
    require(match is not None, "Missing function: " + name)
    finish = end_pair(mask(text), match.end() - 1, "{", "}")
    return text[match.start():finish]


def all_functions(folder):
    result = {}
    pattern = re.compile(r"RECOMP_FUNC void (\w+)\([^\n]*\) \{")
    for path in sorted(folder.glob("funcs_*.c")):
        text = path.read_text(encoding="utf-8")
        code = mask(text)
        for match in pattern.finditer(text):
            name = match.group(1)
            require(name not in result, "Duplicate generated definition: " + name)
            finish = end_pair(code, match.end()-1, "{", "}")
            result[name] = text[match.start():finish]
    return result


def block(body, begin=BEGIN, end=END):
    return begin + "\n" + body.rstrip() + "\n" + end + "\n"


def section(row):
    return (f'[[section]]\nname="{row["name"]}"\nrom=0x{row["rom"]:X}\n'
            f'vram=0x{row["vram"]:X}\nsize=0x{row["size"]:X}\n'
            f'functions=[{{name="{row["name"]}",vram=0x{row["vram"]:X},size=0x{row["size"]:X}}}]\n')


def baseline_symbol_text(text, spec):
    correction = spec["symbol_correction"]
    if correction["after"] in text:
        require(text.count(correction["after"]) == 1, "Ambiguous corrected symbol")
        text = text.replace(correction["after"], correction["before"], 1)
    begin, end = BEGIN.replace("//", "#"), END.replace("//", "#")
    if begin in text:
        require(text.count(begin) == text.count(end) == 1, "Ambiguous recovery symbol block")
        text = text[:text.index(begin)].rstrip() + "\n"
    require(digest(text.encode()) == spec["baseline_symbol_text_sha256"], "Base symbol bytes differ")
    return text


def generate(spec, rom_path, generator, work, qualification=None):
    rom = rom_path.read_bytes()
    require(len(rom) == spec["rom_bytes"] and digest(rom) == spec["rom_sha256"], "ROM identity mismatch")
    for row in spec["functions"] + spec["external_dependencies"]:
        require(digest(rom[row["rom"]:row["rom"] + row["size"]]) == row["sha256"],
                "Original function bytes differ: " + row["name"])
    overlaps = []
    for i,left in enumerate(spec["functions"]):
        for right in spec["functions"][i+1:]:
            size = min(left["vram"]+left["size"],right["vram"]+right["size"])-max(left["vram"],right["vram"])
            if size > 0: overlaps.append((left["name"],right["name"],size))
    require(overlaps == [(r["left"],r["right"],r["bytes"]) for r in spec["allowed_overlaps"]],
            "Unreviewed recovered-body overlap")
    overlaps = []
    for i,left in enumerate(spec["functions"]):
        for right in spec["functions"][i+1:]:
            size = min(left["vram"]+left["size"],right["vram"]+right["size"])-max(left["vram"],right["vram"])
            if size > 0: overlaps.append((left["name"],right["name"],size))
    require(overlaps == [(r["left"],r["right"],r["bytes"]) for r in spec["allowed_overlaps"]],
            "Unreviewed recovered-body overlap")
    for row in spec["direct_calls"]:
        word = struct.unpack_from(">I", rom, row["rom"])[0]
        require(word == row["word"] and word >> 26 == 3 and
                (((row["pc"] + 4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)) == row["target"],
                "Original JAL receipt differs")
    for row in spec["constant_indirect"]["original_words"]:
        require(struct.unpack_from(">I", rom, row["rom"])[0] == row["word"], "Constant JALR proof differs")
    gen_hash = digest(generator.read_bytes())
    if gen_hash != spec["qualified_generator_sha256"]:
        require(qualification is not None and
                qualification.get("generator_sha256") == gen_hash and
                qualification.get("rom_sha256") == spec["rom_sha256"] and
                qualification.get("canonical_functions_verified") == spec["base_function_count"] and
                qualification.get("canonical_manifest_sha256") == spec["baseline_canonical_manifest_sha256"] and
                qualification.get("recovered_closure_functions_verified") == 545 and
                qualification.get("source_manifest_sha256"),
                "Generator needs exact base canonical/source/closure qualification")
    # Even qualified source-built variants must reproduce every new body digest below.

    work.mkdir(parents=True, exist_ok=True)
    output = work / "generated"
    require(not output.exists(), "Generation output must be fresh")
    symbols = work / "symbols.toml"
    symbols.write_text("".join(section(row) for row in spec["functions"] + spec["external_dependencies"]), encoding="utf-8", newline="\n")
    config = work / "config.toml"
    config.write_text("[input]\nsymbols_file_path=" + json.dumps(symbols.as_posix()) +
        "\nrom_file_path=" + json.dumps(rom_path.resolve().as_posix()) +
        "\noutput_func_path=" + json.dumps(output.as_posix()) +
        "\nfunctions_per_output_file=500\n[patches]\nignored=" +
        json.dumps([row["name"] for row in spec["external_dependencies"]]) + "\n", encoding="utf-8", newline="\n")
    run = subprocess.run([str(generator.resolve()), str(config.resolve())], capture_output=True, text=True)
    (work / "generator.log").write_text(run.stdout + run.stderr, encoding="utf-8")
    require(run.returncode == 0, "Generator failed; inspect generator.log")
    bodies = all_functions(output)
    require(set(bodies) == {r["name"] for r in spec["functions"]}, "Generated body set differs")
    for row in spec["functions"]:
        require(digest(bodies[row["name"]].encode()) == row["generated_body_sha256"],
                "Generated body differs: " + row["name"])
    patch = spec["constant_indirect"]
    body = bodies[patch["owner"]]
    require(body.count(patch["before"]) == 1, "Constant JALR source differs")
    bodies[patch["owner"]] = body.replace(patch["before"], patch["after"], 1)
    return bodies, gen_hash


def transform(repo, spec, bodies):
    folder = repo / SP / "generated/ares_boot_to_combat"
    raw = {name: (folder / name).read_bytes() for name in spec["base_inputs"]}
    require(all(digest(raw[n]) == r["sha256"] for n,r in spec["base_inputs"].items()),
            "Recovery requires the exact base guest-input bytes")
    texts = {name: data.decode("utf-8") for name,data in raw.items()}
    normalize = lambda text: text.replace("\r\n", "\n")
    texts = {n: normalize(t) for n,t in texts.items()}
    correction = spec["identity_correction"]
    old_body = function_body(texts[correction["file"]], correction["old_name"])
    require(digest(old_body.encode()) == correction["old_body_sha256"], "Wrong-identity body preimage differs")
    texts[correction["file"]] = texts[correction["file"]].replace(old_body, bodies[correction["new_name"]], 1)
    for call in spec["direct_calls"]:
        name = call["file"]
        owner = function_body(texts[name], call["owner"])
        old = call["old_name"] + "(rdram, ctx);"
        new = call["new_name"] + "(rdram, ctx);"
        require(owner.count(old) == call["owner_old_call_count"], "Direct named caller preimage differs")
        # Two JALs to one target in the same owner, if present, are patched as one verified group.
        if old in texts[name]:
            texts[name] = texts[name].replace(owner, owner.replace(old, new), 1)
    # Every receipt now resolves to the intended named generated body, never the stop alias.
    for call in spec["direct_calls"]:
        owner = function_body(texts[call["file"]], call["owner"])
        require(call["new_name"] + "(rdram, ctx);" in owner and
                call["old_name"] + "(rdram, ctx);" not in owner, "Direct call still bound to old owner")
    added = [r for r in spec["functions"] if r["name"] != correction["new_name"]]
    code = "\n".join(bodies[r["name"]] for r in added)
    target = spec["append_file"]
    require(BEGIN not in texts[target], "Recovery already installed")
    texts[target] = texts[target].rstrip() + "\n\n" + block(code)
    declarations = "\n".join(f'void {r["name"]}(uint8_t*,recomp_context*);' for r in added)
    header = texts["funcs.h"].replace(correction["old_name"], correction["new_name"])
    anchor = "#ifdef __cplusplus\n}\n#endif"
    require(header.count(anchor) == 1, "Header anchor differs")
    texts["funcs.h"] = header.replace(anchor, block(declarations) + anchor, 1)
    overlays = texts["recomp_overlays.inl"]
    for pair in spec["overlay_corrections"]:
        require(overlays.count(pair["before"]) == 1, "Identity registration preimage differs")
        overlays = overlays.replace(pair["before"], pair["after"], 1)
    arrays, entries = [], []
    for offset, row in enumerate(added):
        index = spec["base_num_sections"] + offset
        name = f'section_{index}_{row["name"]}_funcs'
        arrays.append(f'static FuncEntry {name}[] = {{{{ .func = {row["name"]}, .offset = 0, .rom_size = 0x{row["size"]:X} }}}};')
        entries.append(f'{{ .rom_addr = 0x{row["rom"]:08X}, .ram_addr = 0x{row["vram"]:08X}, .size = 0x{row["size"]:X}, .funcs = {name}, .num_funcs = 1, .relocs = nullptr, .num_relocs = 0, .index = {index} }},')
    anchor = "static SectionTableEntry section_table[] = {"
    require(overlays.count(anchor) == 1, "Section array anchor differs")
    overlays = overlays.replace(anchor, block("\n".join(arrays)) + anchor, 1)
    anchor = "};\nconst size_t num_sections = "
    require(overlays.count(anchor) == 1, "Section entries anchor differs")
    overlays = overlays.replace(anchor, block("\n".join(entries), BEGIN+" ENTRIES", END+" ENTRIES") + anchor, 1)
    old_count = f'const size_t num_sections = {spec["base_num_sections"]};'
    require(overlays.count(old_count) == 1, "Section count differs")
    overlays = overlays.replace(old_count, f'const size_t num_sections = {spec["base_num_sections"] + len(added)};', 1)
    texts["recomp_overlays.inl"] = overlays
    symbol_path = repo / SP / "symbols/rage_wars.us.ares_boot_to_combat.toml"
    symbol = baseline_symbol_text(symbol_path.read_text(encoding="utf-8"), spec)
    pair = spec["symbol_correction"]
    require(symbol.count(pair["before"]) == 1, "Symbol correction preimage differs")
    symbol = symbol.replace(pair["before"], pair["after"], 1).rstrip() + "\n\n" + block(
        "".join(section(r) for r in added), BEGIN.replace("//","#"), END.replace("//","#"))
    outputs = {}
    for name,text in texts.items():
        # Leave all untouched files byte-exact; changed files retain their reviewed line convention.
        normalized = normalize(raw[name].decode("utf-8"))
        if text == normalized:
            outputs[name] = raw[name]
        else:
            nl = spec["base_inputs"][name]["newline"]
            outputs[name] = (text.replace("\n","\r\n") if nl == "crlf" else text).encode()
    return outputs, symbol.encode()


def apply_layer(repo, spec, rom_path, generator, work, apply=False, author_result=False, qualification=None):
    bodies, gen_hash = generate(spec, rom_path, generator, work, qualification)
    outputs, symbols = transform(repo, spec, bodies)
    hashes = {n: {"sha256":digest(data),"bytes":len(data)} for n,data in outputs.items()}
    result = {"inputs": hashes, "symbol_sha256":digest(symbols)}
    if author_result:
        save(work / "authored-result.json", result)
        return result
    require(result == spec["expected_result"], "Recovery postimage differs from reviewed recipe")
    if apply:
        folder = repo / SP / "generated/ares_boot_to_combat"
        for name,data in outputs.items():
            if (folder/name).read_bytes() != data: (folder/name).write_bytes(data)
        (repo / SP / "symbols/rage_wars.us.ares_boot_to_combat.toml").write_bytes(symbols)
    receipt = {"schema":"xr64.bounded-recovery-replay.v1","rom_sha256":spec["rom_sha256"],
        "generator_sha256":gen_hash,"authoring_generator_sha256":spec["qualified_generator_sha256"],"source_built_qualification":qualification,"generated_functions":len(bodies),"added_registrations":len(bodies)-1,
        "original_direct_JAL_receipts":len(spec["direct_calls"]),"constant_JALR_binding":"proved local target",
        "shared_return_delay_slot_bytes":4,"expected_result":result,"applied":apply,
        "runtime_acceptance":"not established by generation or source checks"}
    save(work / "receipt.json", receipt)
    return receipt


def verify_installed(repo, spec):
    folder = repo / SP / "generated/ares_boot_to_combat"
    for name,row in spec["expected_result"]["inputs"].items():
        require(digest((folder/name).read_bytes()) == row["sha256"], "Installed input differs: "+name)
    require(digest((repo/SP/"symbols/rage_wars.us.ares_boot_to_combat.toml").read_bytes()) ==
            spec["expected_result"]["symbol_sha256"], "Installed symbols differ")
    bodies = all_functions(folder)
    require(len(bodies) == spec["base_function_count"] + len(spec["functions"]) - 1, "Definition count differs")
    overlay = (folder/"recomp_overlays.inl").read_text(encoding="utf-8")
    for row in spec["functions"]:
        require(row["name"] in bodies and len(re.findall(r"\.func\s*=\s*"+re.escape(row["name"])+r"\s*,",overlay)) == 1,
                "Body/registration uniqueness differs: "+row["name"])
    for row in spec["direct_calls"]:
        owner=bodies[row["owner"]]
        require(row["new_name"]+"(rdram, ctx);" in owner and row["old_name"]+"(rdram, ctx);" not in owner,
                "Installed direct binding differs")
    require("LOOKUP_FUNC(0)" not in bodies[spec["identity_correction"]["new_name"]], "Zero lookup remains")
    return {"installed_exact":True,"function_count":len(bodies),"direct_receipts":len(spec["direct_calls"])}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--repo",type=Path,default=ROOT)
    p.add_argument("--spec",type=Path,default=ROOT/"scripts/bounded_recovery_20261006.json")
    p.add_argument("--rom",type=Path)
    p.add_argument("--generator",type=Path)
    p.add_argument("--work",type=Path)
    p.add_argument("--apply",action="store_true")
    p.add_argument("--verify",action="store_true")
    p.add_argument("--author-result",action="store_true")
    args=p.parse_args(); spec=load(args.spec)
    require(spec["schema"]=="xr64.bounded-recovery.v1","Wrong recovery schema")
    if args.verify: result=verify_installed(args.repo.resolve(),spec)
    else:
        require(args.rom and args.generator and args.work,"ROM, generator and fresh work are required")
        result=apply_layer(args.repo.resolve(),spec,args.rom,args.generator,args.work.resolve(),args.apply,args.author_result)
    print(json.dumps({k:v for k,v in result.items() if k != "expected_result"}))
if __name__=="__main__": main()