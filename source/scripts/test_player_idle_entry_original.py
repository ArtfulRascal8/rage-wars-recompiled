#!/usr/bin/env python3
"""Differentially compare native state-2 entry 0022C878 with its generated body."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / 'build/functional-decomp-20261009/player-idle-entry-differential'
GENERATED = ROOT / 'staging/rage-wars-recomp-spike/generated/ares_boot_to_combat/funcs_20.c'
SYMBOL = 'trace_func_0022C878_r0002D478'
BODY_SHA256 = '429d8da21e855e38772af49ec4f26dfd13aed13719f7345809ae2292ca33ff79'
ENTRY, EXTENT, ROM_OFFSET = 0x0022C878, 0x48, 0x0002D478
ROM_SHA1 = '9f8cb190831945f6f707aa8c2b20e04fa4276795'
ROM_WINDOW_SHA256 = '328bf42a2f411cb146bdcb6a6087c49b23ef064bc7ee15dccf03800109d0f5f6'
RUNTIME_HEADER_SHA256 = 'd42a572ec50e15f8196fdeb7a37ff62e62f30c80d80bf87c454298ece8fce9d6'
EXPECTED_CASES = 6


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def child_environment():
    # MSBuild rejects an environment block that contains both Path and PATH.
    env = dict(os.environ)
    values = [value for key, value in env.items() if key.casefold() == 'path']
    for key in list(env):
        if key.casefold() == 'path':
            del env[key]
    entries, seen = [], set()
    for value in values:
        for entry in value.split(os.pathsep):
            if not entry:
                continue
            normalized = os.path.normcase(os.path.normpath(entry))
            if normalized not in seen:
                seen.add(normalized)
                entries.append(entry)
    env['Path'] = os.pathsep.join(entries)
    return env


def extract_body(source):
    signature = ('RECOMP_FUNC void ' + SYMBOL + '(').encode()
    begin = source.index(signature)
    brace = source.index(b'{', begin)
    depth = 0
    in_string = None
    escaped = False
    for i in range(brace, len(source)):
        c = source[i]
        if in_string:
            if escaped:
                escaped = False
            elif c == 92:
                escaped = True
            elif c == in_string:
                in_string = None
        elif c in (34, 39):
            in_string = c
        elif c == 123:
            depth += 1
        elif c == 125:
            depth -= 1
            if depth == 0:
                return source[begin:i + 1]
    raise ValueError('unbalanced generated idle-entry body')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--cmake', type=Path, default=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'))
    args = parser.parse_args()
    out = args.output.resolve()
    permitted = DEFAULT_OUT.resolve()
    if out != permitted and permitted not in out.parents:
        raise ValueError('Differential outputs must remain under the ignored functional-decomp evidence directory')

    header = ROOT / 'toolchain/N64ModernRuntime/N64Recomp/include/recomp.h'
    if digest(header.read_bytes()) != RUNTIME_HEADER_SHA256:
        raise ValueError('Pinned runtime header changed')
    atlas = ROOT / 'docs/RAGE_WARS_ADDRESS_ATLAS.md'
    atlas_text = atlas.read_text(encoding='ascii')
    atlas_identity = f'{ENTRY:08X} / {ROM_OFFSET:08X} +{EXTENT:04X}'
    if atlas_identity not in atlas_text or ROM_WINDOW_SHA256 not in atlas_text:
        raise ValueError('The exact idle-entry ROM window is not qualified in the current Address Atlas')

    source_bytes = GENERATED.read_bytes().replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    body = extract_body(source_bytes)
    if digest(body) != BODY_SHA256:
        raise ValueError('Pinned generated idle-entry original body changed')
    addresses = sorted({int(a, 16) for a in re.findall(rb'// 0x([0-9A-Fa-f]{8}):', body)})
    if addresses != list(range(ENTRY, ENTRY + EXTENT, 4)):
        raise ValueError('Idle-entry instruction comments do not cover the qualified ROM extent')

    out.mkdir(parents=True, exist_ok=True)
    (out / 'generated_original.inc').write_bytes(body)
    native = ROOT / 'xr64/games/rage_wars/native_player'
    harness = ROOT / 'scripts/player_idle_entry_original_contract.cpp'
    sources = [harness, native / 'idle_entry.cpp', native / 'idle_entry.hpp',
               native / 'actor_storage.hpp', GENERATED, atlas,
               ROOT / 'docs/CHARACTER_BEHAVIOR_RECORD_MAP.md',
               ROOT / 'scripts/character_callback_audit.json', Path(__file__).resolve()]
    before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}

    cmake_text = '''cmake_minimum_required(VERSION 3.24)
set(CMAKE_SYSTEM_VERSION 10.0.22621.0 CACHE STRING "Pinned SDK")
project(player_idle_entry_original_contract LANGUAGES CXX)
add_executable(player_idle_entry_original_contract "@HARNESS@" "@CORE@")
target_include_directories(player_idle_entry_original_contract PRIVATE "@NATIVE@" "@RECOMP@" "@OUT@")
target_compile_features(player_idle_entry_original_contract PRIVATE cxx_std_20)
target_compile_definitions(player_idle_entry_original_contract PRIVATE NDEBUG)
if(MSVC)
 target_compile_options(player_idle_entry_original_contract PRIVATE /fp:strict /O2 /W4)
else()
 message(FATAL_ERROR "This differential requires the selected Windows MSVC toolchain")
endif()
'''
    for token, path in [('HARNESS', harness), ('CORE', native / 'idle_entry.cpp'),
                        ('NATIVE', native), ('RECOMP', header.parent), ('OUT', out)]:
        cmake_text = cmake_text.replace('@' + token + '@', path.as_posix())
    (out / 'CMakeLists.txt').write_text(cmake_text, encoding='utf-8')

    exe = out / 'compiled/Release/player_idle_entry_original_contract.exe'
    commands = [[str(args.cmake), '-S', str(out), '-B', str(out / 'compiled'),
                 '-G', 'Visual Studio 17 2022', '-A', 'x64', '-T', 'v143,version=14.43.34808'],
                [str(args.cmake), '--build', str(out / 'compiled'), '--config', 'Release'],
                [str(exe)]]
    runs = []
    failure = None
    passed = []
    env = child_environment()
    for number, command in enumerate(commands):
        start = time.monotonic()
        result = subprocess.run(command, text=True, capture_output=True, env=env)
        log = out / f'run_{number}.log'
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        runs.append({'command': command, 'returncode': result.returncode,
                     'seconds': round(time.monotonic() - start, 3), 'log': log.name})
        if result.returncode:
            failure = number
            print(result.stdout + result.stderr)
            break
        if number == 2:
            passed = re.findall(r'^PASS (.+)$', result.stdout, re.M)
            total = re.search(r'^TOTAL_PASS (\d+)$', result.stdout, re.M)
            if not total or int(total.group(1)) != len(passed) or len(passed) != EXPECTED_CASES:
                raise ValueError('Missing or malformed idle-entry differential completion evidence')
            print(result.stdout)

    after = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    if after != before:
        raise ValueError('Native/test inputs changed during differential run')
    receipt = {
        'schema': 'xr64.player-idle-entry.original-body-differential.v1',
        'status': 'PASS' if failure is None else 'FAIL',
        'target': {'guest': '0022C878', 'rom_offset': '0002D478', 'rom_extent_bytes': '00000048',
                   'instruction_words': len(addresses), 'rom_sha1': ROM_SHA1,
                   'atlas_window_sha256': ROM_WINDOW_SHA256,
                   'generated_body_sha256_lf': BODY_SHA256},
        'semantic_reference': {'revision': '89cdfe7f61d449682335368cdcf918f4cb6cb056',
                               'label_source': 'current character-state record map; state-2 idle label remains debug-derived',
                               'use': 'Semantic cross-check only; no reference source code copied'},
        'oracle': 'Pinned current generated US v1.0 callback body; 002227D0 state setter is a controlled fixture',
        'classification': 'Synthetic native-core vs original-generated-body differential; not original-game observation',
        'toolchain': 'MSVC v143 14.43.34808, SDK 10.0.22621.0, x64, C++20, /fp:strict /O2, NDEBUG',
        'passed_cases': passed if failure is None else [],
        'passed_case_count': len(passed) if failure is None else 0,
        'sources': before,
        'runtime_header_sha256': RUNTIME_HEADER_SHA256,
        'runs': runs,
        'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest() if exe.exists() else None,
        'game_build_or_launch': False,
        'limitations': ['No captured player state or observed gameplay',
                        '002227D0 is a controlled state-setter fixture in this core differential',
                        'Volatile registers and floating-point condition-code state are not compared'],
    }
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if failure is not None:
        raise SystemExit(1)
    print('PASS', EXPECTED_CASES, 'idle-entry differential cases; receipt:', out / 'receipt.json')


if __name__ == '__main__':
    main()
