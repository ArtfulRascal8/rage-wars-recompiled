#!/usr/bin/env python3
"""Execute real consumer-transformed fatal lookup paths and crash report UI."""
import argparse, ctypes, hashlib, json, re, subprocess, time
from ctypes import wintypes as w
from pathlib import Path
from prepare_demo_source import transform, mask, end_pair
ROOT = Path(__file__).resolve().parents[1]

def extract(source, name):
    code = mask(source)
    pattern = r'(?<![\w:])' + re.escape(name) + r'\s*\('
    for m in re.finditer(pattern, code):
        end = end_pair(code, code.index('(', m.start()), '(', ')')
        tail = end
        while code[tail].isspace(): tail += 1
        if code[tail] != '{': continue
        start = code.rfind('\n', 0, m.start()) + 1
        return source[start:end_pair(code, tail, '{', '}')]
    raise ValueError('Definition missing: ' + name)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--cmake',default=r'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
    a=ap.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    host=ROOT/'xr64/games/rage_wars/recomp_host/src';runtime=ROOT/'toolchain/N64ModernRuntime'
    overlays=transform((runtime/'librecomp/src/overlays.cpp').read_text(encoding='utf-8'),'overlays.cpp',consumer_release=True)[0]
    definitions='\n'.join(extract(overlays,n) for n in ('recomp::overlays::set_callable_resolver','recomp::overlays::set_callable_failure_handler','get_function','recomp::overlays::find_resident_callable','find_callable_with_identity','recomp::overlays::get_active_callable_identity','recomp::overlays::resolve_callable_with_identity','get_function_with_context'))
    main_source=transform((host/'main.cpp').read_text(encoding='utf-8'),'main.cpp',consumer_release=True)[0]
    forwarder=extract(main_source,'xr64_rage_wars_get_function_with_context_trace')
    assert 'return ::get_function_with_context(rdram, ctx, vram);' in forwarder
    assert 'note_lookup' not in forwarder
    fixture=r'''
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <mutex>
#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "rage_wars_crash_report.hpp"
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
static std::unordered_map<int32_t,recomp_func_t*> func_map;
static std::mutex callable_map_mutex;
static std::unordered_map<int32_t,recomp::overlays::CallableMappingIdentity> callable_identities;
static recomp::overlays::callable_resolver_t callable_resolver=nullptr;
static recomp::overlays::callable_failure_handler_t callable_failure_handler=nullptr;
static unsigned g_host_thread=0,g_host_entry=0;
static void trace_callable_fault(const char*,int32_t,bool,bool,recomp_func_t*) {}
// ACTUAL_FUNCTIONS
static unsigned resolved=0;
static void success(uint8_t*,recomp_context*) {}
static recomp_func_t* resolver(uint8_t*,recomp_context*,int32_t) { ++resolved; return success; }
static unsigned notifications=0;
static void unexpected(recomp_context*,int32_t) { ++notifications; }
static recomp_func_t* lookup_via_macro(uint8_t* rdram,recomp_context* ctx,int32_t vram) {
    return LOOKUP_FUNC(vram);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 90;
    std::wstring mode=argv[1];
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    xr64::rage_wars::crash::initialize(argv[2],mode==L"dialog");
    recomp_context ctx{};ctx.r4=0x80123450;ctx.r5=12;ctx.r6=7;ctx.r7=0;
    ctx.r29=0x807FF000;ctx.r31=0x0022A574;
    if(mode==L"success") {
        recomp::overlays::set_callable_failure_handler(unexpected);
        func_map.emplace(0x00231F4C,success);
        if(get_function(0x00231F4C)!=success)return 91;
        if(get_function_with_context(nullptr,&ctx,0x00231F4C)!=success)return 92;
        if(lookup_via_macro(nullptr,&ctx,0x00231F4C)!=success)return 94;
        func_map.clear();recomp::overlays::set_callable_resolver(resolver);
        if(lookup_via_macro(nullptr,&ctx,0x00231F4C)!=success || resolved!=1 || notifications)return 93;
        return 0;
    }
    recomp::overlays::set_callable_failure_handler(xr64::rage_wars::crash::missing_callable);
    if(mode==L"native") { RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return 94; }
    if(mode==L"terminate") { std::terminate(); }
    if(mode==L"plain") { get_function(0x00231F4C);return 95; }
    lookup_via_macro(nullptr,&ctx,0x00231F4C);
    return 96;
}
'''
    (out/'fixture.cpp').write_text(fixture.replace('// ACTUAL_FUNCTIONS',definitions+'\n'+forwarder),encoding='utf-8')
    reporter_source=transform((host/'rage_wars_crash_report.cpp').read_text(encoding='utf-8'),'rage_wars_crash_report.cpp',consumer_release=True)[0]
    assert 'Report saved in your Rage Wars profile.' in reporter_source
    (out/'reporter.cpp').write_text(reporter_source,encoding='utf-8')
    version_rc='''#include <windows.h>
1 VERSIONINFO
 FILEVERSION 0,2,0,1
 PRODUCTVERSION 0,2,0,1
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0
 FILEOS 0x00040004L
 FILETYPE VFT_APP
 FILESUBTYPE 0
 BEGIN
  BLOCK "StringFileInfo"
  BEGIN
   BLOCK "040904B0"
   BEGIN
    VALUE "ProductVersion", "0.2.0-beta.1"
    VALUE "FileVersion", "0.2.0-beta.1"
   END
  END
  BLOCK "VarFileInfo"
  BEGIN
   VALUE "Translation", 0x0409, 1200
  END
 END
'''
    (out/'version.rc').write_text(version_rc,encoding='utf-8')
    cm='cmake_minimum_required(VERSION 3.24)\nproject(crash_report_contract LANGUAGES CXX RC)\nadd_executable(crash_report_contract fixture.cpp reporter.cpp version.rc)\ntarget_compile_features(crash_report_contract PRIVATE cxx_std_20)\n'
    cm+='target_include_directories(crash_report_contract PRIVATE '+ ' '.join('"'+p.as_posix()+'"' for p in (host,ROOT/'toolchain/N64Recomp/include',runtime/'librecomp/include'))+')\n'
    (out/'CMakeLists.txt').write_text(cm,encoding='utf-8')
    subprocess.run([a.cmake,'-S',str(out),'-B',str(out/'compiled'),'-A','x64'],check=True)
    subprocess.run([a.cmake,'--build',str(out/'compiled'),'--config','Release'],check=True)
    exe=out/'compiled/Release/crash_report_contract.exe';binary=exe.read_bytes();digest=hashlib.sha256(binary).hexdigest().upper()
    assert 'Report saved:'.encode('utf-16le') not in binary
    assert 'Report saved in your Rage Wars profile.'.encode('utf-16le') in binary
    run_root=out/'runs'/str(time.time_ns());run_root.mkdir(parents=True)
    passed=[]
    for mode in ('success','context','plain','native','terminate'):
        directory=run_root/('profile-'+mode)/'crashes'
        run=subprocess.run([str(exe),mode,str(directory)],capture_output=True,timeout=15)
        if mode=='success':
            assert run.returncode==0 and not directory.exists();passed.append('resident/resolver success has no report');continue
        assert run.returncode!=0,(mode,run.returncode)
        text=(directory/'crash-latest.txt').read_text(encoding='utf-8')
        assert digest in text and '0.2.0-beta.1' in text
        assert 'C:/Users/' not in text and 'C:\\\\Users\\\\' not in text and 'PRIVATE' not in text
        assert 'Guest registers' not in text and 'A0=' not in text and 'SP=' not in text
        assert 'Active indirect calls:' not in text and 'Invocation registers (raw):' not in text
        assert 'Last indirect lookup' not in text and 'Lookup source:' not in text
        assert str(run_root) not in text
        archive=list(directory.glob('crash-2*.txt'));assert len(archive)==1 and archive[0].read_bytes()==(directory/'crash-latest.txt').read_bytes()
        if mode in ('context','plain'):
            assert run.returncode==1 and 'Failure: unresolved guest callable' in text
            assert 'Missing guest function: 0x00231F4C' in text
        if mode=='context':assert 'Version: 0.2.0-beta.1' in text
        if mode=='plain':assert 'Guest registers' not in text
        if mode=='native':assert 'unhandled native exception' in text and '0xC0000005' in text and 'Fault module: ' in text and '.dll' in text and 'Fault module RVA:' not in text and 'Access address:' not in text
        if mode=='terminate':assert 'Failure: C++ terminate' in text
        passed.append(mode+' fatal path saves exact hash and archive')
    bad=run_root/'not-a-directory';bad.write_text('fixture',encoding='utf-8')
    run=subprocess.run([str(exe),'context',str(bad/'crashes')],capture_output=True,timeout=15)
    assert run.returncode==1 and b'Missing guest function: 0x00231F4C' in run.stderr
    passed.append('unwritable report directory preserves fatal exit and stderr report')
    u=ctypes.WinDLL('user32',use_last_error=True);k=ctypes.WinDLL('kernel32',use_last_error=True)
    u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
    u.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
    u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
    u.GetClipboardData.argtypes=[w.UINT];u.GetClipboardData.restype=w.HANDLE
    u.OpenClipboard.argtypes=[w.HWND];u.OpenClipboard.restype=w.BOOL
    k.GlobalLock.argtypes=[w.HANDLE];k.GlobalLock.restype=ctypes.c_void_p
    k.GlobalUnlock.argtypes=[w.HANDLE]
    directory=run_root/'profile-ui-unicode-\u03a9'/'crashes'
    process=subprocess.Popen([str(exe),'dialog',str(directory)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        window=None;deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            window=u.FindWindowW('#32770','Rage Wars crash report')
            pid=w.DWORD()
            if window:u.GetWindowThreadProcessId(window,ctypes.byref(pid))
            if window and pid.value==process.pid:break
            time.sleep(.1)
        else:raise AssertionError('Native Copy Report dialog did not appear')
        assert process.poll() is None
        assert 'Report saved in your Rage Wars profile.' in binary.decode('utf-16le',errors='ignore')
        assert 'Report saved:' not in binary.decode('utf-16le',errors='ignore')
        u.SendMessageW(window,0x400+102,101,0) # TDM_CLICK_BUTTON
        assert process.poll() is None,'Copy must keep the dialog open'
        assert u.OpenClipboard(None)
        handle=u.GetClipboardData(13);assert handle
        pointer=k.GlobalLock(handle);assert pointer
        copied=ctypes.wstring_at(pointer);k.GlobalUnlock(handle);u.CloseClipboard()
        assert copied.encode('utf-8')==(directory/'crash-latest.txt').read_bytes()
        assert digest in copied and '0.2.0-beta.1' in copied and 'Missing guest function: 0x00231F4C' in copied
        assert 'Guest registers' not in copied and str(run_root) not in copied
        u.SendMessageW(window,0x400+102,102,0)
        assert process.wait(timeout=10)==1
        passed.append('compiled crash footer hides profile path; native UI Copy Report equals saved report and Close Game exits; Unicode profile')
    finally:
        if process.poll() is None:process.kill();process.wait()
    evidence={'passed':passed,'fixture_sha256':digest,'coverage':'real consumer-transformed lookup functions and reporter; injected faults, not campaign reproduction'}
    (out/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(evidence,indent=2))
if __name__=='__main__':main()
