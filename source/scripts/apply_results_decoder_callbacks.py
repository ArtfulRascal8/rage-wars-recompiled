"""Apply one ROM-verified decoder dispatch and update the reviewed source recipe.

Authoring helper only; normal clean builds replay guest-function-edits.json.
Requires an unchanged caller, exact original ROM, and all registered dependency bodies.
"""
from pathlib import Path
import argparse, json, re, sys, struct, tomllib
sys.dont_write_bytecode = True
p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--rom', type=Path, required=True)
p.add_argument('--proof', type=Path, required=True)
p.add_argument('--source-candidate', type=Path)
p.add_argument('--canonical', type=Path)
a = p.parse_args()
sys.path.insert(0, str(a.repo / 'scripts'))
from reconstruction_common import split_functions, digest, make_edit, require
proof = json.loads(a.proof.read_text())
rom = a.rom.read_bytes()
require(digest(rom) == proof['rom_sha256'], 'Callback recovery invariant failed')
G = a.repo / 'staging/rage-wars-recomp-spike/generated/ares_boot_to_combat'
fs, layouts = split_functions(G)
caller = proof['caller']
before = fs[caller]
require(digest(before.encode()) == proof['functions'][caller]['generated_sha256'], 'Caller changed; re-review required')
for name, row in proof['functions'].items():
    require(digest(rom[row['rom']:row['rom'] + row['size']]) == row['rom_sha256'], name)
    require(digest(fs[name].encode()) == row['generated_sha256'], name)
    regs = (G / 'recomp_overlays.inl').read_text()
    require(len(re.findall('\\.func\\s*=\\s*' + name + '\\s*,', regs)) == 1, name)
for pc, word in proof['selection_words'].items():
    require(struct.unpack_from('>I', rom, int(pc, 16) - 2094080)[0] == int(word, 16), 'Callback recovery invariant failed')
old = '    LOOKUP_FUNC(ctx->r23)(rdram, ctx);\n'
require(before.count(old) == 1, 'Callback recovery invariant failed')
new = '    // The original instructions select only these three ROM-backed decoders.\n    // Resolve this closed family like fixed JALs; unrelated targets retain lookup.\n    switch ((uint32_t)ctx->r23) {\n'
for address, name in proof['targets'].items():
    new += f'        case {address}U: {name}(rdram, ctx); break;\n'
new += '        default: LOOKUP_FUNC(ctx->r23)(rdram, ctx); break;\n    }\n'
after = before.replace(old, new)
changed = []
for name, segments in layouts.items():
    if any((s.get('function') == caller for s in segments)):
        file = G / name
        raw = file.read_bytes()
        oldraw = before.replace('\n', '\r\n').encode() if b'\r\n' in raw else before.encode()
        newraw = after.replace('\n', '\r\n').encode() if b'\r\n' in raw else after.encode()
        require(raw.count(oldraw) == 1, 'Callback recovery invariant failed')
        file.write_bytes(raw.replace(oldraw, newraw))
        changed.append(name)
if a.source_candidate:
    require(a.canonical is not None, 'Callback recovery invariant failed')
    src = a.source_candidate
    recipe = src / 'reconstruction'
    canon, _ = split_functions(a.canonical)
    edits = json.loads((recipe / 'guest-function-edits.json').read_text())
    edits[caller] = make_edit(canon[caller], after)
    (recipe / 'guest-function-edits.json').write_text(json.dumps(edits, indent=2) + '\n', encoding='utf-8')
    expected = json.loads((recipe / 'expected-guest-inputs.json').read_text())
    for name in changed:
        raw = (G / name).read_bytes()
        expected[name].update(sha256=digest(raw), bytes=len(raw))
    (recipe / 'expected-guest-inputs.json').write_text(json.dumps(expected, indent=2) + '\n', encoding='utf-8')
    notice = src / 'MODIFICATIONS.txt'
    notice.write_bytes(notice.read_bytes() + b'\n2026-10-05: Preserve the original locally selected three-decoder callback family at 0x0025F21C when transient guest callable mappings are unavailable. No decoder body or game data changed.\n')
    manifest = json.loads((src / 'source-manifest.json').read_text())
    for rel in ['reconstruction/guest-function-edits.json', 'reconstruction/expected-guest-inputs.json', 'MODIFICATIONS.txt']:
        raw = (src / rel).read_bytes()
        manifest['files'][rel].update(sha256=digest(raw), bytes=len(raw))
    (src / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'changed_generated': changed, 'caller_after_sha256': digest(after.encode()), 'verified_original_rom_bodies': len(proof['functions']), 'new_guest_functions': 0}, indent=2))
