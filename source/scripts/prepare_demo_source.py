#!/usr/bin/env python3
"""Create build-local sources with an explicit diagnostic allowlist removed.
Original gameplay callbacks and editable source files are never rewritten.
"""
from pathlib import Path
import argparse,hashlib,json,re
PROBES={
 'xr64_native_menu_trace','xr64_rw017_trace_guest_pc',
 'xr64_rw044_producer_milestone','xr64_rw045_producer_transition',
 'xr64_rw046_producer_transition','xr64_rw047_caller_transition',
 'xr64_rw048_message_transition','xr64_rw049_startup_frontier',
 'xr64_rw050_initialization_frontier','xr64_rw051_config_chain_frontier',
 'xr64_rw052_config_gate_transition','xr64_rw053_initializer_frontier',
 'xr64_rw054_accessor_transition','xr64_rw076_trace_callsite',
 'xr64_rw076_trace_null_offset_access','ultramodern::runtime_trace',
}
LEX=re.compile(r'R"(?P<delim>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delim)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',re.S)
def mask(text):
 return LEX.sub(lambda m:''.join('\n' if c=='\n' else ' ' for c in m.group()),text)
def end_pair(code,start,left,right):
 depth=0
 for i in range(start,len(code)):
  if code[i]==left:depth+=1
  elif code[i]==right:
   depth-=1
   if depth==0:return i+1
 raise ValueError('Unbalanced source delimiters')

def remove_definition(text,name):
 code=mask(text)
 pattern=re.compile(r'(?<![\w:])'+re.escape(name)+r'\s*\(')
 definitions=[]
 for match in pattern.finditer(code):
  end=end_pair(code,code.index('(',match.start()),'(',')')
  tail=end
  while tail<len(code) and code[tail].isspace():tail+=1
  if tail<len(code) and code[tail]=='{':
   definitions.append((match.start(),end_pair(code,tail,'{','}')))
 if len(definitions)!=1:raise ValueError(f'Expected one {name} definition, found {len(definitions)}')
 start,end=definitions[0]
 start=text.rfind('\n',0,start)+1
 return text[:start]+'\n'*text[start:end].count('\n')+text[end:]

def replace_definition_body(text,name,body):
 code=mask(text)
 pattern=re.compile(r'(?<![\w:])'+re.escape(name)+r'\s*\(')
 definitions=[]
 for match in pattern.finditer(code):
  end=end_pair(code,code.index('(',match.start()),'(',')')
  tail=end
  while tail<len(code) and code[tail].isspace():tail+=1
  if tail<len(code) and code[tail]=='{':
   definitions.append((tail,end_pair(code,tail,'{','}')))
 if len(definitions)!=1:raise ValueError(f'Expected one {name} definition, found {len(definitions)}')
 start,end=definitions[0]
 replacement=body+'\n'*max(0,text[start:end].count('\n')-body.count('\n'))
 return text[:start]+replacement+text[end:]

def replace_once(text,pattern,replacement,label):
 text,count=re.subn(pattern,replacement,text,flags=re.M)
 if count!=1:raise ValueError(f'Expected one {label}, found {count}')
 return text

def consumer_transform(text,name,counts):
 if name=='main.cpp':
  text=replace_definition_body(text,'xr64_rage_wars_get_function_with_context_trace',
   '{\n    (void)file;\n    (void)line;\n    return ::get_function_with_context(rdram, ctx, vram);\n}')
  counts['consumer_replaced_lookup_trace_with_context_forwarder']=1
  for symbol in ('xr64_rage_wars_note_recovered_rom_entry',
                 'xr64_rage_wars_invoke_function_with_context_trace'):
   text=remove_definition(text,symbol)
   counts['consumer_removed_'+symbol]=1
 if name=='rage_wars_demo_launcher.cpp':
  text=replace_once(text,
   r'^[ \t]*if\(arg==L"--input-replay"\)\{[^\n]*\}.*$',
   '', 'launcher input replay command-line branch')
  counts['consumer_removed_launcher_input_replay_cli']=1
 if name=='gate5_main.cpp':
  text=replace_once(text,
   r'^[ \t]*else if \(arg == "--input-replay"\) \{[^\n]*\}',
   '', 'input replay command-line branch')
  counts['consumer_removed_input_replay_cli']=1
  text=replace_once(text,
   r'^[ \t]*g_options\.input_replay\s*=\s*startup\.input_replay;[ \t]*$',
   '    g_options.input_replay.clear();', 'input replay startup assignment')
  counts['consumer_forced_input_replay_disabled']=1
  text=text.replace(' [--input-replay <path>]','')
  text=replace_once(text,r'^[ \t]*_putenv_s\("XR64_RW_INPUT_REPLAY", ""\);',
   '', 'input replay environment clear')
  counts['consumer_removed_input_replay_environment']=1
  for symbol in ('trace_input_replay','rw_replay_diag_emit','trace_input_replay_diagnostic'):
   text=remove_definition(text,symbol)
   counts['consumer_removed_'+symbol]=1
 if name=='rage_wars_crash_report.cpp':
  text='#define XR64_PUBLIC_RELEASE 1\n'+text
 return text

def transform(text,name,audio_producer_hook=False,consumer_release=False):
 if audio_producer_hook and name=='funcs_35.c':
  original='RECOMP_FUNC void trace_func_002B3974_r000B4574('
  if text.count(original)!=1:raise ValueError('Expected exactly one audio producer definition')
  text=text.replace(original,'RECOMP_FUNC void xr64_audio_command_build_body(',1)
 code=mask(text);edits=[];counts={}
 voids=set(PROBES)
 if name=='gate5_main.cpp':
  voids.update({'trace','trace_input_replay','trace_rw017_peripheral','trace_rw073_order','trace_graphics_task'})
 if name=='rage_wars_desktop_renderer.cpp':
  voids.update({'capture_framebuffer','trace_mesh','draw_diagnostic_hud','log_gl_errors'})
 pattern=re.compile(r'(?<![\w:])(?:'+ '|'.join(re.escape(n) for n in sorted(voids|{'std::fprintf','fprintf','std::printf','printf','std::fflush','fflush','std::getenv','getenv'},key=len,reverse=True))+r')\s*\(')
 covered=0
 for m in pattern.finditer(code):
  if m.start()<covered:continue
  line=code[code.rfind('\n',0,m.start())+1:m.start()]
  if line.lstrip().startswith('#'):continue
  symbol=code[m.start():code.index('(',m.start())].strip()
  start=code.index('(',m.start());end=end_pair(code,start,'(',')')
  tail=end
  while tail<len(code) and code[tail].isspace():tail+=1
  args=text[start+1:end-1]
  if symbol in voids:
   if tail<len(code) and code[tail]=='{':
    close=end_pair(code,tail,'{','}')
    edits.append((tail,close,'{}'));covered=close
   elif re.search(r'\bvoid\s*$',code[max(0,m.start()-32):m.start()]):
    continue # A forward declaration is part of the ABI, not a probe call.
   else:edits.append((m.start(),end,'((void)0)'))
  elif symbol.endswith('getenv'):
   if not re.search(r'"XR64_(?:RW_|NATIVE_MENU_ROW_TRACE)',args):continue
   edits.append((m.start(),end,'((const char*)0)'))
  elif symbol.endswith('fprintf'):
   if not re.match(r'\s*stderr\s*,',args):continue
   if any(prefix in args for prefix in ('"RW_LOCAL_PRESENT ','"RW_LOCAL_SAMPLE ','"RW_LOCAL_SAMPLE_INPUT ')):continue # bounded opt-in responsive-control evidence
   edits.append((m.start(),end,'0'))
  elif symbol.endswith('fflush'):
   if args.strip() not in ('stdout','stderr'):continue
   edits.append((m.start(),end,'0'))
  else:edits.append((m.start(),end,'0'))
  covered=max(covered,edits[-1][1]) # Do not also rewrite nested calls inside a removed expression.
  counts[symbol]=counts.get(symbol,0)+1
 for start,end,replacement in reversed(edits):
  replacement+='\n'*(text[start:end].count('\n')-replacement.count('\n'))
  text=text[:start]+replacement+text[end:]
 if consumer_release:text=consumer_transform(text,name,counts)
 return text,counts

def native_player_view_transform(text, name):
 if name != 'funcs_18.c': return text, 0
 signature = 'RECOMP_FUNC void trace_func_00220A5C_r0002165C(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified player-camera producer')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = 'a14bd753cb9ea4aa01d46074b3278abf39794a14ea5e8ad1217b1b6791004e0c'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('PlayerView original producer preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_view_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for PlayerView declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_view_bridge.h"', 1)
 return text, 1

def native_ground_movement_transform(text, name):
 if name != 'funcs_19.c': return text, 0
 signature = 'RECOMP_FUNC void trace_func_002233CC_r00023FCC(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified ground movement integrator')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = 'a29b81d1d17766a910562604d2a13f6664a33d84e15f5a9022811c9a980f966a'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('GroundMovement original preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_ground_movement_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for GroundMovement declaration')
 text = text.replace(include, include + '\n#include "rage_wars_ground_movement_bridge.h"', 1)
 return text, 1

def native_player_ease_transform(text, name):
 if name != 'funcs_7.c': return text, 0
 signature = 'RECOMP_FUNC void resident_wave14_func_00274860(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified Player_Tick ease helper')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = 'de1bbe68b79c9af2510cb46d5ed9c9567c772f78ab864250b835c5d6c18923a3'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('00274860 original body preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_ease_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for PlayerEase declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_ease_bridge.h"', 1)
 return text, 1

def native_ground_contact_transform(text, name):
 if name != 'funcs_3.c': return text, 0
 signature = 'RECOMP_FUNC void resident_wave14_func_0024E60C(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified ground-contact query')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = '5e6460afad76c6e8de8e9ac9fefbc788dcb58e3e6398348916ad2a74bf893e33'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('0024E60C original body preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_ground_contact_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for GroundContact declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_ground_contact_bridge.h"', 1)
 return text, 1

def native_damage_descriptor_transform(text, name):
 if name != 'funcs_17.c': return text, 0
 signature = 'RECOMP_FUNC void trace_func_00216488_r00017088(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified damage-descriptor writer')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = 'e762f166285fe7516f2585861d973a4f84a3771ca03b172213c000127685b18f'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('00216488 original body preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_damage_descriptor_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for DamageDescriptor declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_damage_descriptor_bridge.h"', 1)
 return text, 1

def native_floor_height_transform(text, name):
 if name != 'funcs_20.c': return text, 0
 signature = 'RECOMP_FUNC void trace_func_0022C054_r0002CC54(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified floor-height blend')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = '0ddf9f9fd967eaf2db46f34aa6907442f174f7376e9d6de8bb08b7909de76c8e'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('0022C054 original body preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_floor_height_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for FloorHeight declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_floor_height_bridge.h"', 1)
 return text, 1

def native_surface_lookup_transform(text, name):
 if name != 'funcs_23.c': return text, 0
 signature = 'RECOMP_FUNC void trace_func_0024E7BC_r0004F3BC(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified surface lookup wrapper')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = '9878d1d79b524b9a108f0337e60d3e2bfa0ebd4e488a2bcad62ee887c6a30621'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('0024E7BC original body preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_surface_lookup_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for SurfaceLookup declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_surface_lookup_bridge.h"', 1)
 return text, 1

def native_player_movement_speed_transform(text, name):
 if name != 'funcs_19.c': return text, 0
 signature = 'RECOMP_FUNC void trace_func_002228E4_r000234E4(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified Player_Tick movement-speed helper')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = '085f5effe1aede36a46ec1e2db219dfbcd39ce1162b4425b26f23d70bc2dcb07'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('002228E4 original body preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_movement_speed_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for MovementSpeed declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_movement_speed_bridge.h"', 1)
 return text, 1

def native_player_idle_entry_transform(text, name):
 if name != 'funcs_20.c': return text, 0
 signature = 'RECOMP_FUNC void trace_func_0022C878_r0002D478(uint8_t* rdram, recomp_context* ctx) {'
 if text.count(signature) != 1: raise ValueError('Expected one qualified idle-state entry callback')
 start = text.index(signature)
 opening = start + len(signature) - 1
 end = end_pair(mask(text), opening, '{', '}')
 expected = '429d8da21e855e38772af49ec4f26dfd13aed13719f7345809ae2292ca33ff79'
 if hashlib.sha256(text[start:end].encode('utf-8')).hexdigest() != expected:
  raise ValueError('0022C878 original body preimage differs; qualify the changed body before bridging')
 replacement = signature + '\n    if (xr64_player_idle_entry_try_update(rdram, ctx)) return;'
 text = text[:start] + text[start:].replace(signature, replacement, 1)
 include = '#include "recomp.h"'
 if text.count(include) != 1: raise ValueError('Expected one recomp header for IdleEntry declaration')
 text = text.replace(include, include + '\n#include "rage_wars_player_idle_entry_bridge.h"', 1)
 return text, 1

def main():
 p=argparse.ArgumentParser();p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--audio-producer-hook',action='store_true')
 p.add_argument('--call-journal',action='store_true');p.add_argument('--call-journal-table',type=Path)
 p.add_argument('--consumer-release',action='store_true')
 # Requires the rage_wars_ground_movement_bridge library in the game link.
 p.add_argument('--native-ground-movement',action='store_true')
 # Experimental scalar ease replacement; the runtime bridge remains opt-in too.
 p.add_argument('--native-player-ease',action='store_true')
 # Experimental ground-contact query; the runtime bridge remains opt-in too.
 p.add_argument('--native-player-ground-contact',action='store_true')
 # Experimental damage-payload constructor; the runtime bridge remains opt-in too.
 p.add_argument('--native-player-damage-descriptor',action='store_true')
 # Experimental fixed-factor floor height blend; the runtime bridge remains opt-in too.
 p.add_argument('--native-player-floor-height',action='store_true')
 # Experimental surface-table query; the runtime bridge remains opt-in too.
 p.add_argument('--native-player-surface-lookup',action='store_true')
 # Experimental movement-speed update; the runtime bridge remains opt-in too.
 p.add_argument('--native-player-movement-speed',action='store_true')
 # Experimental state-2 idle-entry callback; the runtime bridge remains opt-in too.
 p.add_argument('--native-player-idle-entry',action='store_true')
 a=p.parse_args()
 if a.consumer_release and a.call_journal:raise ValueError('Consumer release cannot include the development call journal')
 raw=a.input.read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n').replace('\r','\n')
 text,counts=transform(source,a.input.name,a.audio_producer_hook,a.consumer_release)
 text,native_view_hooks=native_player_view_transform(text,a.input.name)
 if native_view_hooks: counts['native_player_view_opt_in_entry']=native_view_hooks
 text,native_move_hooks=native_ground_movement_transform(text,a.input.name) if a.native_ground_movement else (text,0)
 if native_move_hooks: counts['native_ground_movement_opt_in_entry']=native_move_hooks
 text,native_ease_hooks=native_player_ease_transform(text,a.input.name) if a.native_player_ease else (text,0)
 if native_ease_hooks: counts['native_player_ease_opt_in_entry']=native_ease_hooks
 text,native_ground_hooks=native_ground_contact_transform(text,a.input.name) if a.native_player_ground_contact else (text,0)
 if native_ground_hooks: counts['native_player_ground_contact_opt_in_entry']=native_ground_hooks
 text,native_damage_hooks=native_damage_descriptor_transform(text,a.input.name) if a.native_player_damage_descriptor else (text,0)
 if native_damage_hooks: counts['native_player_damage_descriptor_opt_in_entry']=native_damage_hooks
 text,native_floor_hooks=native_floor_height_transform(text,a.input.name) if a.native_player_floor_height else (text,0)
 if native_floor_hooks: counts['native_player_floor_height_opt_in_entry']=native_floor_hooks
 text,native_surface_hooks=native_surface_lookup_transform(text,a.input.name) if a.native_player_surface_lookup else (text,0)
 if native_surface_hooks: counts['native_player_surface_lookup_opt_in_entry']=native_surface_hooks
 text,native_speed_hooks=native_player_movement_speed_transform(text,a.input.name) if a.native_player_movement_speed else (text,0)
 if native_speed_hooks: counts['native_player_movement_speed_opt_in_entry']=native_speed_hooks
 text,native_idle_hooks=native_player_idle_entry_transform(text,a.input.name) if a.native_player_idle_entry else (text,0)
 if native_idle_hooks: counts['native_player_idle_entry_opt_in_entry']=native_idle_hooks
 journal=[]
 if a.call_journal:
  if a.call_journal_table is None:raise ValueError('Call journal requires the exact active overlay table')
  from prepare_call_journal import instrument_calls
  text,journal=instrument_calls(text,a.input.name,a.call_journal_table.read_text(encoding='utf-8-sig'))
  # First-entry evidence for newly recovered native bodies is source identity,
  # distinct from a loaded guest mapping. Keep all editable ROM bodies intact.
  if a.input.name.startswith('funcs_'):
   from prepare_call_journal import table_function_identities
   identities=table_function_identities(a.call_journal_table.read_text(encoding='utf-8-sig'))
   def recovered_entry(m):
    identity=identities[m[1]]
    return m[0]+f'xr64_rage_wars_note_recovered_rom_entry(0x{identity["vram"]:08X}U, 0x{identity["rom"]:08X}U, {identity["section_index"]}U, ctx);'
   text=re.sub(r'RECOMP_FUNC\s+void\s+(rom_(?:jal|worker)_\w+)\([^)]*\)\s*\{',recovered_entry,text)

 # Portable logical source names keep __FILE__ diagnostics useful without disclosing build-machine paths.
 logical=a.input.resolve().relative_to(Path(__file__).resolve().parents[1]).as_posix()
 text='#line 1 '+json.dumps(logical)+'\n'+text
 a.output.parent.mkdir(parents=True,exist_ok=True)
 a.output.write_text(text,encoding='utf-8')
 a.output.with_suffix(a.output.suffix+'.audit.json').write_text(json.dumps({
  'source':str(a.input),'source_sha256':hashlib.sha256(raw).hexdigest(),
  'output_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'removed':counts,
  'audio_producer_hook':bool(a.audio_producer_hook and a.input.name=='funcs_35.c'),'call_journal_sites':journal,
  'consumer_release':a.consumer_release},indent=2)+'\n')
if __name__=='__main__':main()
