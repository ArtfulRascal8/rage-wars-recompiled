from pathlib import Path
import json,re,struct,hashlib,collections
import argparse
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,default=root/'build/demo-release-20260911');parser.add_argument('--exe',type=Path);parser.add_argument('--version',default='0.2.1-beta.1');args=parser.parse_args()
build=args.build.resolve();exe=args.exe if args.exe else build/'Release/RageWarsDemo.exe'
data=exe.read_bytes()
forbidden=[b'XR64_CALL_FAULT_JOURNAL',b'XR64_FAULT_FRAME_CAPTURE',b'XR64_RW_REPLAY_DIAGNOSTICS',
 b'XR64_RW_CAPTURE_DIR',b'XR64_RW_MEMORY_CAPTURE',b'XR64_RW_DIAG_TASK',b'XR64_RW_INPUT_REPLAY',
 b'XR64_NATIVE_MENU_ROW_TRACE',b'RW090_MEMORY_CAPTURE',b'rdram-host.bin',
 b'RW_ROM_BODY_ENTRY',b'raw_ra=0x',b'raw_sp=0x',b'RW076_REPLAY_EVENT',b'RW076_REPLAY_ARM',
 b'RW076_REPLAY_CONFIG',b'RW076_REPLAY_POLL_STATE',b'RW076_INPUT_PRESS',
 b'Active indirect calls:',b'Invocation registers (raw):',b'Guest registers (raw,',
 b'Last indirect lookup (may precede',b'Lookup source:',b'Fault module RVA:',
 b'Access address:',b'Access operation (',b'xr64_rage_wars_invoke_function_with_context_trace',
 b'xr64_rage_wars_note_recovered_rom_entry',b'--input-replay',b'diagnostic-replay',
 b'--developer',b'--audio-validation-run',b'--trace',b'--summary',b'--headless',b'--max-seconds',
 b'[diagnostic options]',b'capture-next-task',b'pre-swap.bmp',b'presented-front.bmp',b'XR64-render-captures',
 b'glReadPixels',b'glGetTexImage',b'Report saved:',b'C:/Users/',b'C:\\Users\\']
def encoded_forms(value):return (value,value.decode('ascii').encode('utf-16le'))
found=[x.decode('ascii') for x in forbidden if any(form in data for form in encoded_forms(x))]
pe=struct.unpack_from('<I',data,0x3c)[0];optional=pe+24
count=struct.unpack_from('<H',data,pe+6)[0];optional_size=struct.unpack_from('<H',data,pe+20)[0]
assert struct.unpack_from('<H',data,optional)[0]==0x20b
subsystem=struct.unpack_from('<H',data,optional+68)[0]
debug_rva,debug_size=struct.unpack_from('<II',data,optional+112+6*8)
sections=[]
for i in range(count):
 offset=optional+optional_size+i*40
 vs,va,rs,rp=struct.unpack_from('<IIII',data,offset+8);sections.append((va,max(vs,rs),rp))
def file_offset(rva):
 for va,size,rp in sections:
  if va<=rva<va+size:return rp+rva-va
 raise ValueError(rva)
debug_types=[]
if debug_size:
 off=file_offset(debug_rva)
 debug_types=[struct.unpack_from('<I',data,off+i+12)[0] for i in range(0,debug_size,28)]
cache_path=build/'CMakeCache.txt'
assert cache_path.is_file(),'Public release audit requires its CMake cache'
cache={}
for line in cache_path.read_text(encoding='utf-8',errors='replace').splitlines():
 m=re.match(r'([^:#]+):[^=]+=([^;]*)$',line)
 if m:cache[m[1]]=m[2]
required_options={'XR64_PUBLIC_RELEASE':'ON','XR64_CALL_FAULT_JOURNAL':'OFF',
 'XR64_FAULT_FRAME_CAPTURE':'OFF','XR64_RW_REPLAY_DIAGNOSTICS':'OFF',
 'XR64_RENDER_DIAGNOSTICS':'OFF'}
assert {k:cache.get(k) for k in required_options}==required_options,(
 'Release diagnostic options are not in the required state: '+repr({k:cache.get(k) for k in required_options}))
totals=collections.Counter();sources=0;definition_count=0
audit_paths=list((build/'demo-sources').rglob('*.audit.json'))
assert audit_paths,'No consumer-transformed source receipts were produced'
for audit_path in audit_paths:
 audit=json.loads(audit_path.read_text());src=Path(audit['source']);out=Path(str(audit_path)[:-len('.audit.json')])
 assert audit.get('consumer_release') is True,('Source was not consumer-transformed',str(src))
 original=src.read_bytes();derived=out.read_bytes()
 assert hashlib.sha256(original).hexdigest()==audit['source_sha256']
 assert hashlib.sha256(derived).hexdigest()==audit['output_sha256']
 names=lambda b:re.findall(rb'RECOMP_FUNC\s+void\s+(\w+)\s*\(',b)
 expected=names(original)
 if audit.get('audio_producer_hook',False):
  assert src.name=='funcs_35.c' and expected.count(b'trace_func_002B3974_r000B4574')==1
  expected=[b'xr64_audio_command_build_body' if n==b'trace_func_002B3974_r000B4574' else n for n in expected]
 assert expected==names(derived),(str(src),'game callback set changed')
 definition_count+=len(names(original));totals.update(audit['removed']);sources+=1
version_bytes=args.version.encode('ascii')
assert version_bytes in data or version_bytes.decode().encode('utf-16le') in data,(
 'Executable version resource does not contain '+args.version)
required_report_fields=['RAGE WARS RECOMPILED CRASH REPORT v3','Version: ',
 'Executable SHA256: ','Failure: ','Missing guest function: ']
missing_report_fields=[field for field in required_report_fields if field.encode('ascii') not in data]
result={'sha256':hashlib.sha256(data).hexdigest().upper(),'bytes':len(data),'windows_gui':subsystem==2,
 'debug_directory_types':debug_types,'has_codeview_symbols':2 in debug_types,
 'product_version':args.version,'ordinary_crash_report_fields':required_report_fields,
 'missing_crash_report_fields':missing_report_fields,
 'forbidden_capture_probe_trace_strings':found,'consumer_release_cmake_options':required_options,
 'derived_sources':sources,'recompiled_function_definitions_preserved':definition_count,
 'removed_diagnostic_sites':dict(totals)}
(build/'production-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
assert not found and not missing_report_fields and subsystem==2 and 2 not in debug_types
