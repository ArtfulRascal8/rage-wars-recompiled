#!/usr/bin/env python3
"""Verify/reapply ROM-pinned local-player controls and nested menu Back hooks."""
from pathlib import Path
import argparse, hashlib, json
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--apply', action='store_true')
parser.add_argument('--rom', type=Path, default=ROOT/'staging/rage-wars-recomp-spike/input/rage_wars.us.v1.0.z64')
args = parser.parse_args()
rom = args.rom.read_bytes()
assert hashlib.sha256(rom).hexdigest() == '0433043aaba2649bdd1fe717c4020550ac663c0362527aa082490f5977a3e46b'
rows = json.loads(Path(__file__).with_name('modern_controls_hooks.json').read_text())
for row in rows:
    assert hashlib.sha256(rom[row['rom']:row['rom']+row['size']]).hexdigest() == row['rom_body_sha256']
    path = ROOT/row['file']
    source = path.read_text()
    start = source.index('RECOMP_FUNC void '+row['symbol']+'(')
    end = source.find('RECOMP_FUNC', start+12)
    if end < 0: end = len(source)
    body = source[start:end]
    assert body.count(row['anchor']) == 1, 'Guest anchor changed: '+row['symbol']
    expected = row['code']+row['anchor'] if row['before'] else row['anchor']+row['code']
    if expected not in body:
        assert args.apply and row['code'] not in body, 'Missing/moved hook: '+row['symbol']
        body = body.replace(row['anchor'], expected, 1)
        path.write_text(source[:start]+body+source[end:])
print(f'PASS {len(rows)} ROM-pinned controls/menu hooks')
