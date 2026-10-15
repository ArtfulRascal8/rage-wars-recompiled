#!/usr/bin/env python3
"""Pinned original-body/native GroundMovement (002233CC) differential; no game launch/build."""
from pathlib import Path
import argparse, hashlib, json, re, struct, subprocess, time
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / 'build/functional-decomp-20261009/ground-movement-differential'
GENERATED = ROOT / 'staging/rage-wars-recomp-spike/generated/ares_boot_to_combat/funcs_19.c'
SYMBOL = 'trace_func_002233CC_r00023FCC'
BODY_LF = 'bd5ba0ec3b95171d88f98543cb2f12fe5cb3bdbc41ae0bc758f9d5b7aceff233'  # identical to HEAD 4988fbc
ENTRY, WORDS, ROM_OFFSET = 0x002233CC, 316, 0x00023FCC
ROM_WINDOW = 'd6605b47fca4ba7af52cfcc97e86b39948e7591729dc675df756caf0e59cad91'
ROM_SHA1 = '9f8cb190831945f6f707aa8c2b20e04fa4276795'
HEADER = 'd42a572ec50e15f8196fdeb7a37ff62e62f30c80d80bf87c454298ece8fce9d6'

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--cmake', type=Path, default=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'))
    args = parser.parse_args()
    out = args.output.resolve(); permitted = DEFAULT_OUT.resolve()
    if out != permitted and permitted not in out.parents: raise ValueError('Outputs must stay in the ignored evidence directory')
    header = ROOT / 'toolchain/N64ModernRuntime/N64Recomp/include/recomp.h'
    if digest(header) != HEADER: raise ValueError('Pinned runtime header changed')
    rom = args.rom.read_bytes()
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1: raise ValueError('ROM identity differs')
    window = rom[ROM_OFFSET:ROM_OFFSET + 4 * WORDS]
    if hashlib.sha256(window).hexdigest() != ROM_WINDOW: raise ValueError('ROM window differs')

    source = GENERATED.read_bytes().replace(b'\r\n', b'\n')
    begin = source.index(('RECOMP_FUNC void ' + SYMBOL + '(').encode())
    end = source.find(b'RECOMP_FUNC void ', begin + 12)
    body = source[begin:end if end >= 0 else len(source)]
    if hashlib.sha256(body).hexdigest() != BODY_LF: raise ValueError('Pinned original body changed')
    addresses = sorted({int(a, 16) for a in re.findall(rb'// 0x([0-9A-F]{8}):', body)})
    if addresses != list(range(ENTRY, ENTRY + 4 * WORDS, 4)): raise ValueError('Original comments do not cover the qualified extent')
    # Every jal in the comments must match the ROM word at that address.
    for site, target in re.findall(rb'// 0x([0-9A-F]{8}): jal +0x([0-9A-F]{8})', body):
        word = struct.unpack('>I', window[int(site, 16) - ENTRY:int(site, 16) - ENTRY + 4])[0]
        if word >> 26 != 3 or ((word & 0x3FFFFFF) << 2) != int(target, 16): raise ValueError('JAL comment differs from ROM')

    out.mkdir(parents=True, exist_ok=True)
    (out / 'generated_original.inc').write_bytes(body)
    native = ROOT / 'xr64/games/rage_wars/native_player'
    sources = [ROOT / 'scripts/ground_movement_original_contract.cpp', native / 'ground_movement.cpp',
               native / 'ground_movement.hpp', native / 'actor_storage.hpp', Path(__file__).resolve()]
    before = {str(p.relative_to(ROOT)): digest(p) for p in sources}
    cmake_text = '''cmake_minimum_required(VERSION 3.24)
set(CMAKE_SYSTEM_VERSION 10.0.22621.0 CACHE STRING "Pinned SDK")
project(ground_movement_original_contract LANGUAGES CXX)
add_executable(ground_movement_original_contract "@HARNESS@" "@CORE@")
target_include_directories(ground_movement_original_contract PRIVATE "@NATIVE@" "@RECOMP@" "@OUT@")
target_compile_features(ground_movement_original_contract PRIVATE cxx_std_20)
target_compile_definitions(ground_movement_original_contract PRIVATE NDEBUG)
if(MSVC)
 target_compile_options(ground_movement_original_contract PRIVATE /fp:strict /O2 /W4)
else()
 message(FATAL_ERROR "This isolated evidence run requires the selected Windows MSVC toolchain")
endif()
'''
    for token, path in [('HARNESS', sources[0]), ('CORE', sources[1]), ('NATIVE', native), ('RECOMP', header.parent), ('OUT', out)]:
        cmake_text = cmake_text.replace('@' + token + '@', path.as_posix())
    (out / 'CMakeLists.txt').write_text(cmake_text, encoding='utf-8')
    exe = out / 'compiled/Release/ground_movement_original_contract.exe'
    commands = [[str(args.cmake), '-S', str(out), '-B', str(out / 'compiled'), '-G', 'Visual Studio 17 2022', '-A', 'x64', '-T', 'v143,version=14.43.34808'],
                [str(args.cmake), '--build', str(out / 'compiled'), '--config', 'Release'],
                [str(exe)], [str(exe), '--negative-control']]
    runs = []; failure = None; passed = []
    for number, command in enumerate(commands):
        start = time.monotonic(); result = subprocess.run(command, text=True, capture_output=True)
        log = out / f'run_{number}.log'; log.write_text(result.stdout + result.stderr, encoding='utf-8')
        runs.append({'command': command, 'returncode': result.returncode, 'seconds': round(time.monotonic() - start, 3), 'log': log.name})
        if number == 3:
            if result.returncode != 1 or 'callback/store sequence mismatch' not in result.stderr:
                failure = number; print(result.stdout + result.stderr); break
        elif result.returncode:
            failure = number; print(result.stdout + result.stderr); break
        if number == 2:
            passed = re.findall(r'^PASS (\S+)', result.stdout, re.M)
            total = re.search(r'TOTAL_PASS (\d+)', result.stdout)
            if not total or int(total.group(1)) != len(passed): raise ValueError('Malformed completion evidence')
            print(result.stdout.splitlines()[-1])
    after = {str(p.relative_to(ROOT)): digest(p) for p in sources}
    if after != before: raise ValueError('Sources changed during run')
    receipt = {
        'schema': 'xr64.ground-movement.original-body-differential.v1',
        'status': 'PASS' if failure is None else 'FAIL',
        'target': {'guest': '002233CC', 'rom_offset': '00023FCC', 'words': WORDS, 'rom_window_sha256': ROM_WINDOW,
                   'rom_sha1': ROM_SHA1, 'generated_file': str(GENERATED.relative_to(ROOT)), 'symbol': SYMBOL,
                   'body_sha256_lf': BODY_LF, 'body_same_as_head': True},
        'oracle': 'Actual pinned generated body, unchanged text; NDEBUG Release disables NAN_CHECK assertions',
        'toolchain': 'MSVC v143 14.43.34808, SDK 10.0.22621.0, x64, C++20, /fp:strict /O2',
        'classification': 'Synthetic native vs original-generated-body differential; not original-game observation',
        'passed_case_count': len(passed) if failure is None else 0,
        'negative_control': 'native first forward-speed store delayed past next callback must fail' if failure is None else 'unverified',
        'sources': before, 'runtime_header_sha256': HEADER, 'runs': runs,
        'executable_sha256': digest(exe) if exe.exists() else None, 'game_build_or_launch': False,
        'limitations': ['Dependencies are deterministic mocks, not the original helper bodies',
                        'Signaling NaN arithmetic, NaN payload propagation and FCSR exception fidelity are not asserted',
                        'No captured game state or observed movement sequence']}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if failure is not None: raise SystemExit(1)
    print('PASS', len(passed), 'cases; negative control failed as expected; receipt:', out / 'receipt.json')

if __name__ == '__main__': main()
