#!/usr/bin/env python3
"""Pinned original-body/native PlayerView differential test; no game launch/build."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, time
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / 'build/native-player-view-20261008/differential'
PINS = {
 '00220A5C': {'file':'funcs_18.c','symbol':'trace_func_00220A5C_r0002165C',
  'body':'3518b169c19303d4be24c0e18b6d3ea864cc6dbd98ac8f50f4daabc6b47d3afd',
  'target':'f7feb060f2c63e0077d297594008360c447b4f79f80a722c7ab3bac9e9d25b3b',
  'slice':'f01277716349a77a788598ae8a631070441ff00dc5a301c5fb17912de6742294',
  'source':'af09ffdb88affbebd88423f5f0764365d839f971ed842c3a125b2f7b25d5fe1c',
  'proof':'53f70e60800894331321aef1dfb3b4a4ff8af952081f07187df63e416d610d19','words':177,'relocations':46},
 '00238F04': {'file':'funcs_22.c','symbol':'trace_func_00238F04_r00039B04',
  'body':'37ceaa2e86aa286680b75d0873a5c252dc38ea2e738a2bca9fe9f530bea77d26',
  'target':'a7a7e0258c70607300a25e54eb5a2140a91a6187fb3edaa2be6ea8ab90bec756',
  'slice':'4f9376bb733e504a43d2a23cea1c1a1cf376f90c1e423e581c830b831ce5bf86',
  'source':'39455325a9b64f0a13a62ab2120a1be1f8ece7ac7bd3ca388813e8d8ec374dab',
  'proof':'029b9fd6aa50f17b86f83fdbddcc8e6037919875d4b37a9e73eafdb7f83ee1e0','words':107,'relocations':19}}
CATALOG = '9d7c369b6aa53973e09640942c27eff45f696cf11e9540c604b90999b4c9f78d'
HEADER = 'd42a572ec50e15f8196fdeb7a37ff62e62f30c80d80bf87c454298ece8fce9d6'
PAIR_HEADER = '7d14546ee39d4f1700b0fd4bb8d97f6e9b95127cc9f8755be08d013c8eb28df9'

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def check_hash(path, expected):
 actual=digest(path)
 if actual!=expected: raise ValueError(f'Pinned identity changed: {path}: {actual} != {expected}')
 return actual

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--decomp-stage',type=Path,required=True)
 parser.add_argument('--recomp-owner',type=Path,required=True)
 parser.add_argument('--output',type=Path,default=DEFAULT_OUT)
 parser.add_argument('--cmake',type=Path,default=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'))
 parser.add_argument('--seed-capture',type=Path)
 parser.add_argument('--seed-sha256')
 parser.add_argument('--seed-actor')
 parser.add_argument('--seed-byte-order',choices=['little','big'])
 parser.add_argument('--seed-class',choices=['historical-native-state','original-ares-one-frame'])
 args=parser.parse_args()
 out=args.output.resolve(); permitted=DEFAULT_OUT.resolve()
 if out!=permitted and permitted not in out.parents: raise ValueError('Differential outputs must remain in the designated ignored evidence directory')
 stage=args.decomp_stage.resolve(); owner=args.recomp_owner.resolve()
 header=owner/'toolchain/N64ModernRuntime/N64Recomp/include/recomp.h'
 catalog_path=stage/'player_catalog.json';check_hash(catalog_path,CATALOG);check_hash(header,HEADER)
 catalog=json.loads(catalog_path.read_text());assert catalog['rom_sha1']=='9f8cb190831945f6f707aa8c2b20e04fa4276795'
 check_hash(stage/'resume_20261008/matched_pair/camera_float_bits.h',PAIR_HEADER)
 out.mkdir(parents=True,exist_ok=True)
 extraction=[];bodies=[]
 for address,pin in PINS.items():
  path=owner/'staging/rage-wars-recomp-spike/generated/ares_boot_to_combat'/pin['file']
  source=path.read_bytes();begin=source.index(('RECOMP_FUNC void '+pin['symbol']+'(').encode());end=source.find(b'RECOMP_FUNC void ',begin+12)
  body=source[begin:end if end>=0 else len(source)]
  if hashlib.sha256(body).hexdigest()!=pin['body']: raise ValueError('Pinned original body changed: '+pin['symbol'])
  target=stage/'targets'/(address+'.s');check_hash(target,pin['target'])
  words=re.findall(rb'/\*\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+\*/',target.read_bytes())
  assert len(words)==pin['words']
  stream=b''.join(bytes.fromhex(word.decode()) for guest,word in words)
  assert hashlib.sha256(stream).hexdigest()==pin['slice']
  target_addresses={int(guest,16) for guest,word in words}
  body_addresses={int(address,16) for address in re.findall(rb'// 0x([0-9A-Fa-f]{8}):',body)}
  assert body_addresses==target_addresses,'Original comments no longer match qualified extent'
  row=next(row for row in catalog['functions'] if row['address']==address)
  assert row['metadata_owners'][0]['symbol']==pin['symbol'] and row['instruction_words']==pin['words']
  frozen=stage/'resume_20261008/matched_pair'/(address+'.c');check_hash(frozen,pin['source'])
  proof=stage/'resume_20261008/root/proofs'/(address+'_frozen_pair_'+address+'.json');check_hash(proof,pin['proof'])
  value=json.loads(proof.read_text());assert value['all_full_words_equal'] and value['word_count']==pin['words'] and value['relocation_count']==pin['relocations']
  extraction.append({'address':address,'symbol':pin['symbol'],'path':str(path),'file_sha256':digest(path),'body_sha256':pin['body'],'body_bytes':len(body),'target_sha256':pin['target'],'word_stream_sha256':pin['slice'],'words':pin['words'],'proof_sha256':pin['proof']})
  bodies.append(body)
 (out/'generated_original.inc').write_bytes(b'\n'.join(bodies))
 seed=None
 if args.seed_capture:
  if not all([args.seed_sha256,args.seed_actor,args.seed_byte_order,args.seed_class]): raise ValueError('Captured state requires digest, actor, byte order, and evidence class')
  capture=args.seed_capture.resolve();check_hash(capture,args.seed_sha256)
  if capture.stat().st_size not in [0x400000,0x800000]: raise ValueError('Unsupported capture size')
  seed={'path':str(capture),'sha256':args.seed_sha256,'bytes':capture.stat().st_size,'actor':args.seed_actor,'byte_order':args.seed_byte_order,'class':args.seed_class,'execution_class':'Synthetic original-body differential seeded from retained historical state; no observed update transition'}
 sources=[ROOT/'scripts/player_view_original_contract.cpp',ROOT/'xr64/games/rage_wars/native_player/player_view.cpp',ROOT/'xr64/games/rage_wars/native_player/player_view.hpp',ROOT/'xr64/games/rage_wars/native_player/actor_storage.hpp']
 before={str(p):digest(p) for p in sources}
 cmake_text='''cmake_minimum_required(VERSION 3.24)
set(CMAKE_SYSTEM_VERSION 10.0.22621.0 CACHE STRING "Pinned SDK")
project(player_view_original_contract LANGUAGES CXX)
add_executable(player_view_original_contract "@HARNESS@" "@CORE@")
target_include_directories(player_view_original_contract PRIVATE "@NATIVE@" "@RECOMP@" "@OUT@")
target_compile_features(player_view_original_contract PRIVATE cxx_std_20)
target_compile_definitions(player_view_original_contract PRIVATE NDEBUG)
if(MSVC)
 target_compile_options(player_view_original_contract PRIVATE /fp:strict /O2 /W4)
else()
 message(FATAL_ERROR "This isolated evidence run requires the selected Windows MSVC toolchain")
endif()
'''
 for token,path in [('HARNESS',sources[0]),('CORE',sources[1]),('NATIVE',sources[1].parent),('RECOMP',header.parent),('OUT',out)]: cmake_text=cmake_text.replace('@'+token+'@',path.as_posix())
 (out/'CMakeLists.txt').write_text(cmake_text,encoding='utf-8')
 commands=[[str(args.cmake),'-S',str(out),'-B',str(out/'compiled'),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.43.34808'],[str(args.cmake),'--build',str(out/'compiled'),'--config','Release']]
 exe=out/'compiled/Release/player_view_original_contract.exe'
 execute=[str(exe)]
 if seed: execute.extend([seed['path'],seed['actor'],seed['byte_order']])
 commands.append(execute)
 commands.append([str(exe),'--negative-control'])
 run=[];failure=None
 for number,command in enumerate(commands):
  start=time.monotonic();result=subprocess.run(command,text=True,capture_output=True)
  log=out/('run_'+str(number)+'.log');log.write_text(result.stdout+result.stderr,encoding='utf-8')
  run.append({'command':command,'returncode':result.returncode,'seconds':round(time.monotonic()-start,3),'log':str(log)})
  if number==3:
   if result.returncode!=1 or 'callback/store sequence mismatch' not in result.stderr:
    failure=number;print(result.stdout+result.stderr);break
  elif result.returncode: failure=number;print(result.stdout+result.stderr);break
  if number==2:
   passed=re.findall(r'^PASS (.+)$',result.stdout,re.M)
   total=re.search(r'TOTAL_PASS (\d+)',result.stdout)
   if not total or int(total.group(1))!=len(passed) or len(passed)<105:raise ValueError('Missing/malformed differential completion evidence')
   print(result.stdout)
 after={str(p):digest(p) for p in sources}
 if after!=before:raise ValueError('Native/test sources changed during differential run')
 receipt={'schema':'xr64.player-view.original-body-differential.v1','status':'PASS' if failure is None else 'FAIL','oracle':'Actual extracted current generated bodies, unchanged byte text; NDEBUG Release disables original NAN_CHECK assertions','build_toolchain':'MSVC v143 14.43.34808, SDK10.0.22621.0, x64, CXX20', 'memory_instrumentation':'Test-only MEM_W lvalue proxy records stores; original body text unchanged. SD/LD stack bookkeeping uninstrumented, original frame/saved ABI checked separately.','classification':'Synthetic native vs original-generated-body differential; not original-game observation','passed_cases':passed if failure is None else [],'passed_case_count':len(passed) if failure is None else 0,'negative_control':'Expected failure from reversed native rotation stores with unchanged final words' if failure is None else 'Unverified','driver_sha256':digest(Path(__file__)),'rounding':'MSVC /fp:strict /O2, FE_TONEAREST plus three finite directional-rounding cases; FTZ/DAZ off inside fixture; flags/NaN hardware exception semantics not asserted','input_identities':extraction,'runtime_header':{'path':str(header),'sha256':HEADER},'catalog_sha256':CATALOG,'sources':before,'captured_seed':seed,'runs':run,'executable_sha256':digest(exe) if exe.exists() else None,'game_build_or_launch':False,'limitations':['Mocked dependencies, including math results, are synthetic','Signaling NaN arithmetic, MIPS-specific NaN payload propagation, and FCSR exception fidelity remain unverified','Historical seed supplies one state, not observed before/after camera behavior']}
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
 if failure is not None:raise SystemExit(1)
 print('PASS original-body differential receipt:',out/'receipt.json')
if __name__=='__main__':main()
