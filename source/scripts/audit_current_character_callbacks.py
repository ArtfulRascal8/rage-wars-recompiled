#!/usr/bin/env python3
"""Reconcile state-table callback targets with current generated bodies and registration."""
from pathlib import Path
import argparse
import hashlib
import json
import re


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def guest_addresses(text):
    return {int(value, 16) for value in re.findall(
        r'\b(?:trace_func_|resident_wave[0-9]+_func_)([0-9A-Fa-f]{8})', text)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    output = args.output.resolve()
    if not output.is_relative_to(repo / 'build'):
        raise ValueError('Audit output must remain under the ignored build directory')

    generated = repo / 'staging/rage-wars-recomp-spike/generated/ares_boot_to_combat'
    table_report = repo / 'scripts/character_callback_audit.json'
    table_map = repo / 'docs/CHARACTER_BEHAVIOR_RECORD_MAP.md'
    atlas = repo / 'docs/RAGE_WARS_ADDRESS_ATLAS.md'
    report = json.loads(table_report.read_text(encoding='utf-8'))
    tables = report['proven_state_tables']
    rows = [row for table in tables for row in table['rows']]
    if len(tables) != 2 or any(len(table['rows']) != 47 for table in tables):
        raise ValueError('Expected the two source-qualified 47-row state tables')
    callback_addresses = sorted({row[key] for row in rows for key in ('entry', 'update') if row[key]})

    source_files = sorted(generated.glob('funcs*.c'))
    source_files.extend([generated / 'funcs.h', generated / 'recomp_overlays.inl',
                         table_report, table_map, atlas])
    if any(not path.is_file() for path in source_files):
        raise ValueError('A current generated source or state-table evidence file is missing')
    source_hashes = {path.relative_to(repo).as_posix(): sha256(path) for path in source_files}

    body_definitions = set()
    for path in source_files:
        if path.suffix == '.c':
            body_definitions.update(guest_addresses(path.read_text(encoding='ascii', errors='ignore')))
    prototypes = guest_addresses((generated / 'funcs.h').read_text(encoding='ascii', errors='ignore'))
    registrations = {int(value, 16) for value in re.findall(
        r'\.ram_addr\s*=\s*0x([0-9A-Fa-f]{8})',
        (generated / 'recomp_overlays.inl').read_text(encoding='ascii', errors='ignore'))}

    targets = []
    for address in callback_addresses:
        targets.append({
            'guest': f'{address:08X}',
            'generated_body': address in body_definitions,
            'prototype': address in prototypes,
            'overlay_registration': address in registrations,
        })
    missing = {key: [entry['guest'] for entry in targets if not entry[key]]
               for key in ('generated_body', 'prototype', 'overlay_registration')}
    passed = not any(missing.values())

    output.parent.mkdir(parents=True, exist_ok=True)
    result = {
        'schema': 'xr64.current-character-callback-registration-audit.v1',
        'status': 'PASS_STATIC_SOURCE_REGISTRATION' if passed else 'MISSING_CURRENT_SOURCE_BINDING',
        'classification': 'Read-only generated-source and overlay-table audit; no runtime observation',
        'table_source': {
            'report': table_report.relative_to(repo).as_posix(),
            'report_sha256': source_hashes[table_report.relative_to(repo).as_posix()],
            'map': table_map.relative_to(repo).as_posix(),
            'map_sha256': source_hashes[table_map.relative_to(repo).as_posix()],
            'tables': len(tables), 'records': len(rows),
            'unique_nonzero_callback_targets': len(callback_addresses),
            'legacy_scan_records': len(report.get('records', [])),
            'legacy_missing_callback_candidates': len(report.get('missing_callback_candidates', [])),
            'legacy_scan_scope_note': 'The legacy 68-record scan is retained as historical evidence; its missing-candidate list is not treated as current source absence.',
        },
        'current_generated_source': {
            'body_definition_count': len(body_definitions),
            'prototype_count': len(prototypes),
            'unique_registered_guest_addresses': len(registrations),
            'missing': missing,
            'targets': targets,
        },
        'runtime': {
            'callback_target_value_observed': False,
            'lookup_func_execution_observed': False,
            'native_availability_established': False,
        },
        'source_sha256': source_hashes,
    }
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({
        'status': result['status'],
        'tables': len(tables),
        'records': len(rows),
        'unique_nonzero_callback_targets': len(callback_addresses),
        'body_definition_count': len(body_definitions),
        'prototype_count': len(prototypes),
        'unique_registered_guest_addresses': len(registrations),
        'missing': missing,
        'output': str(output),
    }, indent=2))
    if not passed:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
