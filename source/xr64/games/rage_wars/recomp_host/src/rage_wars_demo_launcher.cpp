#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shobjidl.h>

#include "rage_wars_demo_launcher.hpp"
#include "rage_wars_demo_rom.hpp"
#include "rage_wars_demo_storage.hpp"

#include <array>
#include <cwchar>
#include <limits>
#include <optional>
#include <system_error>
#include <vector>
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace xr64::rage_wars::demo {
#ifdef XR64_STARTUP_CONTRACT
bool test_setup_dialog(Startup& startup);
#endif
namespace {
constexpr int browse_rom=101,create_or_recover_pak=102,play=103,quit=104,vr_preference=105,calibrate=106,weapon_models=107;
constexpr wchar_t kIniSection[]=L"Loader";
constexpr wchar_t kPreferenceKey[]=L"VrStartup";
struct Paths { std::filesystem::path root, ini, data, legacy_ini; };
struct Dialog {
    Startup value;
    std::filesystem::path ini;
    HWND window=nullptr,rom_text=nullptr,data_text=nullptr,status=nullptr,play_button=nullptr,recover_button=nullptr,preference=nullptr;
    HFONT normal=nullptr,title=nullptr;
    bool accepted=false,valid_rom=false;
    ControllerPakState pak_state=ControllerPakState::Missing;
    std::string pak_error;
    int scale(int n) const {return MulDiv(n,GetDpiForWindow(window),96);}
};
std::wstring widen(const std::string& text) {
    if(text.empty())return {};
    const int size=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(size,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),size);return out;
}
std::optional<std::wstring> read_value(const std::filesystem::path& ini,const wchar_t* key) {
    std::array<wchar_t,32768> value{};
    const DWORD count=GetPrivateProfileStringW(kIniSection,key,L"",value.data(),static_cast<DWORD>(value.size()),ini.c_str());
    if(count>=value.size()-1)return std::nullopt;
    if(count==0)return std::wstring{};
    return std::wstring(value.data(),count);
}
std::filesystem::path local_app_data() {
    std::array<wchar_t,32768> value{};
    const DWORD isolated=GetEnvironmentVariableW(L"XR64_PROFILE_DIR",value.data(),static_cast<DWORD>(value.size()));
    if(isolated && isolated<value.size())return std::filesystem::path(value.data());
    const DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",value.data(),static_cast<DWORD>(value.size()));
    if(count==0||count>=value.size())return {};
    return std::filesystem::path(value.data())/L"XR64"/L"RageWars";
}
std::optional<Paths> make_paths(std::string& error) {
    Paths paths;
    paths.root=local_app_data();
    if(paths.root.empty()){error="Windows did not provide a LocalAppData folder.";return std::nullopt;}
    paths.ini=paths.root/L"launcher.ini";
    paths.data=paths.root/L"user-data";
    std::array<wchar_t,32768> module{};
    const DWORD count=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
    if(count==0||count>=module.size()){error="The application path could not be resolved.";return std::nullopt;}
    paths.legacy_ini=std::filesystem::path(module.data()).parent_path()/L"launcher.ini";
    return paths;
}
bool write_value(const std::filesystem::path& ini,const wchar_t* key,const wchar_t* value,std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(ini.parent_path(),ec);
    if(ec){error="Could not create the per-user settings folder.";return false;}
    std::filesystem::path temporary;
    HANDLE handle=INVALID_HANDLE_VALUE;
    for(unsigned attempt=0;attempt<32;++attempt){
        temporary=ini;temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L"."+std::to_wstring(attempt);
        handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
        if(handle!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS){error="Could not create a temporary launcher settings file.";return false;}
    }
    if(handle==INVALID_HANDLE_VALUE){error="Could not reserve a launcher settings filename.";return false;}
    const unsigned char bom[]={0xFF,0xFE};DWORD written=0;
    const bool bom_ok=WriteFile(handle,bom,2,&written,nullptr)!=0&&written==2&&FlushFileBuffers(handle)!=0;
    CloseHandle(handle);
    if(!bom_ok){DeleteFileW(temporary.c_str());error="Could not initialize the temporary launcher settings file.";return false;}
    if(std::filesystem::exists(ini,ec)&&!ec&&!CopyFileW(ini.c_str(),temporary.c_str(),FALSE)){
        const DWORD code=GetLastError();DeleteFileW(temporary.c_str());error="Could not copy existing launcher settings (Windows error "+std::to_string(code)+").";return false;
    }
    if(ec){DeleteFileW(temporary.c_str());error="Could not inspect existing launcher settings.";return false;}
    SetLastError(ERROR_SUCCESS);
    const BOOL value_written=WritePrivateProfileStringW(kIniSection,key,value,temporary.c_str());
    const DWORD value_error=GetLastError();
    SetLastError(ERROR_SUCCESS);
    const BOOL profile_flushed=WritePrivateProfileStringW(nullptr,nullptr,nullptr,nullptr);
    const DWORD flush_error=GetLastError();
    if(!value_written||!profile_flushed){
        DeleteFileW(temporary.c_str());
        error="Could not write temporary launcher settings (write="+std::to_string(value_error)+", flush="+std::to_string(flush_error)+").";
        return false;
    }    if(!MoveFileExW(temporary.c_str(),ini.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        const DWORD code=GetLastError();DeleteFileW(temporary.c_str());error="Could not atomically replace launcher settings (Windows error "+std::to_string(code)+").";return false;
    }
    return true;
}
std::optional<xr64::rage_wars::recomp::XrStartupPreference> parse_preference(const std::wstring& text) {
    if(_wcsicmp(text.c_str(),L"auto")==0)return xr64::rage_wars::recomp::XrStartupPreference::Auto;
    if(_wcsicmp(text.c_str(),L"on")==0)return xr64::rage_wars::recomp::XrStartupPreference::On;
    if(_wcsicmp(text.c_str(),L"off")==0)return xr64::rage_wars::recomp::XrStartupPreference::Off;
    return std::nullopt;
}
const wchar_t* preference_text(xr64::rage_wars::recomp::XrStartupPreference preference) {
    using P=xr64::rage_wars::recomp::XrStartupPreference;
    switch(preference){case P::Auto:return L"auto";case P::On:return L"on";case P::Off:return L"off";}
    return L"auto";
}
std::filesystem::path saved_rom(const Paths& paths) {
    auto value=read_value(paths.ini,L"ROM");
    if(value&& !value->empty())return *value;
    value=read_value(paths.legacy_ini,L"ROM");
    return value&&!value->empty()?std::filesystem::path(*value):std::filesystem::path{};
}
std::filesystem::path legacy_data(const Paths& paths) {
    auto value=read_value(paths.legacy_ini,L"Data");
    if(value&&!value->empty())return *value;
    std::error_code ec;
    const auto adjacent_default=paths.legacy_ini.parent_path()/L"user-data";
    return std::filesystem::exists(adjacent_default,ec)&&!ec?adjacent_default:std::filesystem::path{};
}
bool same_path(const std::filesystem::path& left,const std::filesystem::path& right) {
    std::error_code ec1,ec2;
    auto a=std::filesystem::weakly_canonical(left,ec1);if(ec1)a=std::filesystem::absolute(left,ec1).lexically_normal();
    auto b=std::filesystem::weakly_canonical(right,ec2);if(ec2)b=std::filesystem::absolute(right,ec2).lexically_normal();
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.native().size()),b.c_str(),static_cast<int>(b.native().size()),TRUE)==CSTR_EQUAL;
}
bool files_equal(const std::filesystem::path& left,const std::filesystem::path& right) {
    std::ifstream a(left,std::ios::binary),b(right,std::ios::binary);
    if(!a||!b)return false;
    return std::vector<char>((std::istreambuf_iterator<char>(a)),{})==std::vector<char>((std::istreambuf_iterator<char>(b)),{});
}
bool migrate_legacy_data(const std::filesystem::path& source,const std::filesystem::path& destination,std::string& error) {
    if(source.empty()||same_path(source,destination))return true;
    std::error_code ec;
    if(!std::filesystem::is_directory(source,ec)||ec)return true;
    std::filesystem::create_directories(destination,ec);
    if(ec){error="Could not create the LocalAppData save folder for migration.";return false;}
    std::filesystem::path archive;
    bool archive_ready=false;
    const auto ensure_archive=[&]() {
        if(archive_ready)return true;
        for(unsigned attempt=0;attempt<32;++attempt){
            archive=destination/(L"legacy-import-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(attempt));
            if(std::filesystem::create_directory(archive,ec)){archive_ready=true;return true;}
            if(ec&&ec!=std::errc::file_exists)return false;
            ec.clear();
        }
        return false;
    };
    for(std::filesystem::recursive_directory_iterator it(source,ec),end;it!=end&&!ec;it.increment(ec)){
        const auto status=it->symlink_status(ec);if(ec)break;
        if(std::filesystem::is_directory(status))continue;
        if(!std::filesystem::is_regular_file(status))continue;
        const auto relative=std::filesystem::relative(it->path(),source,ec);if(ec)break;
        auto target=destination/relative;
        if(std::filesystem::exists(target,ec)&&!ec&&files_equal(it->path(),target))continue;
        const bool conflict=std::filesystem::exists(target,ec)&&!ec;
        if(ec){error="Could not inspect legacy save data during migration.";return false;}
        if(conflict){if(!ensure_archive()){error="Could not reserve a safe archive folder for conflicting legacy data.";return false;}target=archive/relative;}
        std::filesystem::create_directories(target.parent_path(),ec);
        if(ec){error="Could not create a folder while migrating legacy save data.";return false;}
        if(!std::filesystem::copy_file(it->path(),target,std::filesystem::copy_options::none,ec)||ec){error="Could not copy legacy save data without replacing existing files.";return false;}
    }
    if(ec){error="Could not finish reading legacy save data.";return false;}
    return true; // Original files remain in place as a recovery copy.
}
bool has_valid_rom(const std::filesystem::path& path,std::string& error) {
    error.clear();
    if(path.empty()){error="Select your 8 MiB US v1.0 ROM.";return false;}
    const auto ext=path.extension().wstring();
    if(_wcsicmp(ext.c_str(),L".z64")&&_wcsicmp(ext.c_str(),L".v64")&&_wcsicmp(ext.c_str(),L".n64")){
        error="Choose a .z64, .v64, or .n64 N64 ROM.";return false;
    }
    std::vector<std::uint8_t> bytes;
    return load_rom(path,bytes,error);
}
bool pick_rom(HWND owner,std::filesystem::path& path) {
    IFileOpenDialog* dialog=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return false;
    DWORD options=0;dialog->GetOptions(&options);
    dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_FILEMUSTEXIST);
    dialog->SetTitle(L"Select Turok: Rage Wars US v1.0");
    const COMDLG_FILTERSPEC filter={L"Nintendo 64 ROM (.z64, .v64, .n64)",L"*.z64;*.n64;*.v64"};
    dialog->SetFileTypes(1,&filter);
    bool chosen=false;
    if(SUCCEEDED(dialog->Show(owner))){
        IShellItem* item=nullptr;
        if(SUCCEEDED(dialog->GetResult(&item))){
            PWSTR name=nullptr;
            if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))){path=name;CoTaskMemFree(name);chosen=true;}
            item->Release();
        }
    }
    dialog->Release();return chosen;
}
HWND control(Dialog& d,const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND child=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,d.scale(x),d.scale(y),d.scale(w),d.scale(h),d.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(d.normal),TRUE);return child;
}
void refresh(Dialog& d) {
    SetWindowTextW(d.rom_text,d.value.rom.empty()?L"No valid ROM selected":d.value.rom.c_str());
    std::vector<std::uint8_t> bytes;std::string rom_error;
    d.valid_rom=has_valid_rom(d.value.rom,rom_error);
    d.pak_state=d.value.no_pak?ControllerPakState::Valid:inspect_controller_pak(d.value.controller_pak,d.pak_error);
    const bool ready=d.valid_rom&&(d.value.no_pak||d.pak_state==ControllerPakState::Valid);
    EnableWindow(d.play_button,ready);
    EnableWindow(d.recover_button,!d.value.no_pak&&d.pak_state!=ControllerPakState::Valid);
    if(ready)SetWindowTextW(d.status,L"ROM and save are ready.");
    else if(!d.valid_rom)SetWindowTextW(d.status,widen(rom_error).c_str());
    else if(d.pak_state==ControllerPakState::Missing)SetWindowTextW(d.status,L"No Controller Pak exists. Choose Create Pak to initialize an empty save.");
    else SetWindowTextW(d.status,widen(d.pak_error).c_str());
}
bool persist_loader_settings(Dialog& d,std::string& error) {
    if(!write_value(d.ini,L"ROM",d.value.rom.c_str(),error))return false;
    if(!write_value(d.ini,L"Data",d.value.data.c_str(),error))return false;
    return write_value(d.ini,kPreferenceKey,preference_text(d.value.vr_preference),error);
}
bool prepare(Dialog& d) {
    refresh(d);if(!d.valid_rom||(!d.value.no_pak&&d.pak_state!=ControllerPakState::Valid))return false;
    std::error_code ec;std::filesystem::create_directories(d.value.data,ec);
    if(ec){SetWindowTextW(d.status,L"Cannot create the per-user save folder.");return false;}
    const auto probe=d.value.data/(L".write-check-"+std::to_wstring(GetCurrentProcessId()));
    HANDLE file=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(file==INVALID_HANDLE_VALUE){SetWindowTextW(d.status,L"Cannot write to LocalAppData. Check folder permissions.");return false;}
    CloseHandle(file);
    std::string error;
    if(!persist_loader_settings(d,error)){SetWindowTextW(d.status,widen(error).c_str());return false;}
    return true;
}
void recovery_action(Dialog& d) {
    if(d.pak_state==ControllerPakState::Invalid){
        const int answer=MessageBoxW(d.window,
            L"The existing Controller Pak looks invalid. It will be copied to a separate backup before a blank Pak is installed. Continue?",
            L"Recover Controller Pak",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);
        if(answer!=IDYES)return;
    }
    std::filesystem::path backup;std::string error;
    if(!backup_and_reset_controller_pak_atomic(d.value.controller_pak,backup,error)){
        MessageBoxW(d.window,widen(error).c_str(),L"Controller Pak Recovery Failed",MB_OK|MB_ICONERROR);
        return;
    }
    refresh(d);
    if(!backup.empty()){
        const auto message=L"The previous Pak was preserved at:\n"+backup.wstring();
        MessageBoxW(d.window,message.c_str(),L"Controller Pak Backup Created",MB_OK|MB_ICONINFORMATION);
    }
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM w,LPARAM l) {
    Dialog* d=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){d=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);d->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));}
    if(!d)return DefWindowProcW(window,message,w,l);
    switch(message){
    case WM_CREATE:{
        const auto heading=control(*d,L"STATIC",L"RAGE WARS RECOMPILED",0,30,23,620,43);SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(d->title),TRUE);
        control(*d,L"STATIC",L"XR64 STUDIOS  /  PC + VR",0,32,70,620,24);
        control(*d,L"STATIC",L"Game ROM",0,32,112,400,23);
        d->rom_text=control(*d,L"EDIT",L"",WS_BORDER|ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,32,138,486,29);
        control(*d,L"BUTTON",L"Browse...",WS_TABSTOP|BS_PUSHBUTTON,534,137,112,31,browse_rom);
        control(*d,L"STATIC",L"Play mode",0,32,183,240,23);
        d->preference=control(*d,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,32,208,210,180,vr_preference);
        for(const wchar_t* value:{L"Automatic",L"VR",L"PC"})SendMessageW(d->preference,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        const auto pref=static_cast<int>(d->value.vr_preference);SendMessageW(d->preference,CB_SETCURSEL,pref>=0&&pref<=2?pref:0,0);
        control(*d,L"STATIC",L"Saves and settings",0,282,183,300,23);
        d->data_text=control(*d,L"EDIT",d->value.data.c_str(),WS_BORDER|ES_READONLY|ES_AUTOHSCROLL,282,208,364,29);
        control(*d,L"STATIC",L"ROM stays in its original location. Saves, PC settings, and VR settings share this profile.",0,32,260,614,40);
        d->status=control(*d,L"STATIC",L"",0,32,318,614,30);
        control(*d,L"BUTTON",L"Calibrate weapons (VR)",WS_TABSTOP|BS_PUSHBUTTON,32,355,220,36,calibrate);
        control(*d,L"BUTTON",L"Weapon models folder...",WS_TABSTOP|BS_PUSHBUTTON,266,355,240,36,weapon_models);
        d->recover_button=control(*d,L"BUTTON",L"Create Pak",WS_TABSTOP|BS_PUSHBUTTON,282,403,112,36,create_or_recover_pak);
        control(*d,L"BUTTON",L"Exit",WS_TABSTOP|BS_PUSHBUTTON,406,403,112,36,quit);
        d->play_button=control(*d,L"BUTTON",L"Play",WS_TABSTOP|BS_DEFPUSHBUTTON,534,403,112,36,play);
        refresh(*d);return 0;}
    case WM_COMMAND:
        try{switch(LOWORD(w)){
            case weapon_models:{
                IFileOpenDialog* picker=nullptr;
                if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)))){
                    picker->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
                    picker->SetTitle(L"Choose your locally extracted weapon models folder");
                    if(SUCCEEDED(picker->Show(window))){IShellItem* item=nullptr;
                        if(SUCCEEDED(picker->GetResult(&item))){PWSTR name=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))){
                            std::string error;d->value.weapon_assets=name;
                            if(!write_value(d->ini,L"WeaponAssets",name,error))SetWindowTextW(d->status,widen(error).c_str());
                            else SetWindowTextW(d->status,L"Weapon models folder selected.");CoTaskMemFree(name);}item->Release();}}
                    picker->Release();}return 0;}
            case calibrate:d->value.weapon_calibration=true;d->accepted=true;DestroyWindow(window);return 0;
            case browse_rom:if(pick_rom(window,d->value.rom))refresh(*d);return 0;
            case vr_preference:if(HIWORD(w)==CBN_SELCHANGE){const auto selected=static_cast<int>(SendMessageW(d->preference,CB_GETCURSEL,0,0));using P=xr64::rage_wars::recomp::XrStartupPreference;d->value.vr_preference=selected==1?P::On:selected==2?P::Off:P::Auto;}return 0;
            case create_or_recover_pak:recovery_action(*d);return 0;
            case play:case IDOK:if(prepare(*d)){d->accepted=true;DestroyWindow(window);}return 0;
            case quit:case IDCANCEL:DestroyWindow(window);return 0;
        }}catch(const std::exception& e){MessageBoxW(window,widen(e.what()).c_str(),L"Rage Wars Recompiled",MB_OK|MB_ICONERROR);}
        break;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
bool show_setup_dialog(Startup& startup,const Paths& paths) {
#ifdef XR64_STARTUP_CONTRACT
    return test_setup_dialog(startup);
#else
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Dialog d;d.value=startup;d.ini=paths.ini;
    const int dpi=GetDpiForSystem();
    d.normal=CreateFontW(-MulDiv(11,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    d.title=CreateFontW(-MulDiv(26,dpi,72),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.hIcon=LoadIconW(nullptr,MAKEINTRESOURCEW(32512));wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.lpszClassName=L"XR64RageWarsSetup";
    RegisterClassW(&wc);
    const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT rect{0,0,MulDiv(678,dpi,96),MulDiv(468,dpi,96)};AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Rage Wars Recompiled - Setup",style,
        work.left+(work.right-work.left-(rect.right-rect.left))/2,work.top+(work.bottom-work.top-(rect.bottom-rect.top))/2,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,wc.hInstance,&d);
    if(window){ShowWindow(window,SW_SHOW);UpdateWindow(window);SetForegroundWindow(window);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}}
    else show_error("The setup window could not be created.");
    DeleteObject(d.normal);DeleteObject(d.title);if(SUCCEEDED(com))CoUninitialize();
    if(d.accepted)startup=d.value;
    return d.accepted;
#endif
}
bool load_profile(Startup& startup,Paths& paths,std::string& error) {
    auto made=make_paths(error);if(!made)return false;paths=*made;
    startup.data=paths.data;
    if(auto assets=read_value(paths.ini,L"WeaponAssets");assets&&!assets->empty())startup.weapon_assets=*assets;
    auto configured_rom=saved_rom(paths);if(startup.rom.empty())startup.rom=configured_rom;
    const auto legacy=legacy_data(paths);
    if(!migrate_legacy_data(legacy,startup.data,error))return false;
    startup.controller_pak=startup.controller_pak.empty()?startup.data/L"controller.pak":startup.controller_pak;
    return true;
}
}

std::string_view startup_usage(){return "RageWarsRecompiled.exe [--rom <.z64|.v64|.n64>] [--setup] [--vr auto|on|off|--xr|--no-xr] [--controller-pak <path>] [--controller-no-pak] [--headless|--visible] [diagnostic options]";}

xr64::rage_wars::recomp::XrStartupPreference read_vr_startup_preference(){
    using P=xr64::rage_wars::recomp::XrStartupPreference;
    std::string ignored;auto paths=make_paths(ignored);
    if(paths){auto value=read_value(paths->ini,kPreferenceKey);if(value){auto pref=parse_preference(*value);if(pref)return *pref;}}
    if(paths){auto value=read_value(paths->legacy_ini,kPreferenceKey);if(value){auto pref=parse_preference(*value);if(pref)return *pref;}}
    return P::Auto;
}
bool save_vr_startup_preference(xr64::rage_wars::recomp::XrStartupPreference preference,std::string& error){
    error.clear();
    using P=xr64::rage_wars::recomp::XrStartupPreference;
    if(preference!=P::Auto&&preference!=P::On&&preference!=P::Off){error="Unknown VR startup preference.";return false;}
    auto paths=make_paths(error);if(!paths)return false;
    return write_value(paths->ini,kPreferenceKey,preference_text(preference),error);
}

StartupOutcome resolve_startup(int argc,wchar_t** argv,Startup& startup,std::string& error){
    using P=xr64::rage_wars::recomp::XrStartupPreference;
    error.clear();startup=Startup{};
    std::optional<P> cli_preference;bool explicit_rom=false;bool setup=false;bool audio_validation=false;
    auto set_cli_preference=[&](P preference){if(cli_preference&&*cli_preference!=preference){error="Conflicting --vr/--xr startup preferences.";return false;}cli_preference=preference;return true;};
    auto path_arg=[&](int& i,std::filesystem::path& out,const wchar_t* option){if(i+1>=argc){error="Option is missing its path value.";return false;}out=argv[++i];return true;};
    auto count_arg=[&](int& i,std::uint64_t& out,const wchar_t* option){
        if(i+1>=argc){error="Numeric option is missing its value.";return false;}
        wchar_t* end=nullptr;const auto value=std::wcstoull(argv[++i],&end,10);
        if(end==argv[i]||*end!=L'\0'){error="Numeric option has an invalid value.";return false;}out=value;return true;
    };
    for(int i=1;i<argc;++i){const std::wstring arg=argv[i];
        if(arg==L"--help"||arg==L"-h")return StartupOutcome::Help;
        if(arg==L"--audio-validation-run"){audio_validation=true;continue;}
        if(arg==L"--setup"){startup.setup_requested=true;setup=true;continue;}
        if(arg==L"--rom"){if(!path_arg(i,startup.rom,L"--rom"))return StartupOutcome::InvalidArgs;explicit_rom=true;continue;}

        if(arg==L"--controller-pak"){if(!path_arg(i,startup.controller_pak,L"--controller-pak"))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--trace"){if(!path_arg(i,startup.trace,L"--trace"))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--summary"){if(!path_arg(i,startup.summary,L"--summary"))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--input-replay"){if(!path_arg(i,startup.input_replay,L"--input-replay"))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--vr"){
            if(i+1>=argc){error="--vr needs auto, on, or off.";return StartupOutcome::InvalidArgs;}
            auto parsed=parse_preference(argv[++i]);if(!parsed){error="--vr needs auto, on, or off.";return StartupOutcome::InvalidArgs;}
            if(!set_cli_preference(*parsed))return StartupOutcome::InvalidArgs;continue;
        }
        if(arg==L"--xr"){if(!set_cli_preference(P::On))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--no-xr"){if(!set_cli_preference(P::Off))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--max-vi"){if(!count_arg(i,startup.max_vi,L"--max-vi"))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--max-seconds"){if(!count_arg(i,startup.max_seconds,L"--max-seconds"))return StartupOutcome::InvalidArgs;continue;}
        if(arg==L"--stop-at-checkpoint"){
            if(i+1>=argc){error="--stop-at-checkpoint needs a checkpoint name.";return StartupOutcome::InvalidArgs;}
            const std::wstring value=argv[++i];
            startup.stop_checkpoint.clear();
            for(const wchar_t character:value){if(character>0x7F){error="Checkpoint names must be ASCII.";return StartupOutcome::InvalidArgs;}startup.stop_checkpoint.push_back(static_cast<char>(character));}
            continue;
        }
        if(arg==L"--headless"){startup.headless=true;startup.visible=false;startup.presentation_explicit=true;continue;}
        if(arg==L"--visible"){startup.visible=true;startup.headless=false;startup.presentation_explicit=true;continue;}
        if(arg==L"--controller-no-pak"){startup.no_pak=true;continue;}
        if(arg==L"--skip-controller-pak-selftest"){startup.skip_pak_selftest=true;continue;}
        if(arg==L"--mute"){startup.mute=true;continue;}
        if(arg==L"--developer"){startup.developer=true;continue;}
        if(arg==L"--godot-live-bridge"){startup.godot_live_bridge=true;continue;}
        if(arg==L"--rw017-peripheral-trace"){startup.rw017_peripheral_trace=true;continue;}
        if(arg==L"--rw023-force-ares-producer-path"){startup.rw023_force_ares_producer_path=true;continue;}
        if(arg.rfind(L"--",0)==0){error="Unknown command-line option.";return StartupOutcome::InvalidArgs;}
        error="Unexpected command-line argument.";return StartupOutcome::InvalidArgs;
    }
    if(audio_validation) {
        std::array<wchar_t,32768> profile{};
        constexpr std::uint64_t validation_limit = 600;
        if(setup || !explicit_rom || startup.mute || startup.max_seconds<10 || startup.max_seconds>validation_limit ||
            !GetEnvironmentVariableW(L"XR64_PROFILE_DIR",profile.data(),static_cast<DWORD>(profile.size()))) {
            error="Audio validation needs an isolated profile, explicit ROM, audio enabled, and a 10-600 second limit.";
            return StartupOutcome::InvalidArgs;
        }
        // Bounded diagnostics bypass setup but retain the real desktop renderer.
        // Headless here suppresses modal diagnostics; visible owns normal GL rendering.
        startup.headless=true;startup.visible=true;startup.presentation_explicit=true;
    }
    if(startup.max_seconds==0&&startup.max_vi==0)startup.stop_checkpoint="none";
    if(startup.stop_checkpoint!="none"&&startup.stop_checkpoint!="main-handoff"&&startup.stop_checkpoint!="first-vi"&&startup.stop_checkpoint!="first-graphics-task"&&startup.stop_checkpoint!="multiple-graphics-tasks"){
        error="Unsupported --stop-at-checkpoint value.";return StartupOutcome::InvalidArgs;
    }
    const auto explicit_controller_pak=!startup.controller_pak.empty();
    if(startup.no_pak&&explicit_controller_pak){error="--controller-no-pak cannot be combined with --controller-pak.";return StartupOutcome::InvalidArgs;}
    if(!startup.data.empty()&& !explicit_controller_pak)startup.controller_pak=startup.data/L"controller.pak";
    Paths paths;
    if(!load_profile(startup,paths,error))return StartupOutcome::InvalidArgs;
    if(!startup.data.empty())paths.data=startup.data;
    if(startup.controller_pak.empty())startup.controller_pak=startup.data/L"controller.pak";
    if(cli_preference)startup.vr_preference=*cli_preference;else startup.vr_preference=read_vr_startup_preference();
    std::error_code ec;std::filesystem::create_directories(startup.data,ec);
    if(ec){error="The per-user save folder is not writable.";return StartupOutcome::InvalidArgs;}
    if(explicit_rom){
        std::string rom_error;if(!has_valid_rom(startup.rom,rom_error)){
            if(startup.headless){error=rom_error;return StartupOutcome::InvalidArgs;}
            setup=true;
        }
    }
    if(setup&&startup.headless){error="Setup requires visible presentation.";return StartupOutcome::InvalidArgs;}
    if(startup.headless&&startup.rom.empty()){
        error="Headless startup requires an explicit --rom path.";return StartupOutcome::InvalidArgs;
    }
    if(startup.no_pak){
        std::string rom_error;if(!has_valid_rom(startup.rom,rom_error)){
            if(startup.headless){error=rom_error;return StartupOutcome::InvalidArgs;}
            if(!show_setup_dialog(startup,paths))return StartupOutcome::SetupCancelled;
            return StartupOutcome::Ready;
        }
        if(!startup.headless||setup){if(startup.headless){error="Setup requires visible presentation.";return StartupOutcome::InvalidArgs;}if(!show_setup_dialog(startup,paths))return StartupOutcome::SetupCancelled;}
        return StartupOutcome::Ready;
    }
    std::string pak_error;const auto pak_state=inspect_controller_pak(startup.controller_pak,pak_error);
    bool rom_ok=!startup.rom.empty();std::string rom_error;
    if(rom_ok)rom_ok=has_valid_rom(startup.rom,rom_error);
    if(startup.headless){
        if(!rom_ok){error=rom_error.empty()?"Headless startup requires an explicit valid --rom path.":rom_error;return StartupOutcome::InvalidArgs;}
        if(pak_state!=ControllerPakState::Valid){error=pak_error.empty()?"Headless startup requires a valid Controller Pak; run setup to recover it.":pak_error;return StartupOutcome::InvalidArgs;}
        return StartupOutcome::Ready;
    }
    if(!setup&&!rom_ok&&!explicit_rom)setup=true;
    if(!show_setup_dialog(startup,paths))return StartupOutcome::SetupCancelled;
    return StartupOutcome::Ready;
}

bool show_launcher(Startup& startup,bool force_setup){
    std::string error;Paths paths;
    if(!load_profile(startup,paths,error)){show_error(error.c_str());return false;}
    if(startup.vr_preference!=xr64::rage_wars::recomp::XrStartupPreference::Auto&&
       startup.vr_preference!=xr64::rage_wars::recomp::XrStartupPreference::On&&
       startup.vr_preference!=xr64::rage_wars::recomp::XrStartupPreference::Off)
        startup.vr_preference=read_vr_startup_preference();
    if(startup.controller_pak.empty())startup.controller_pak=startup.data/L"controller.pak";
    (void)force_setup; // Interactive launches always offer mode selection.
    if(!show_setup_dialog(startup,paths))return false;
    return true;
}

void show_error(const char* message){MessageBoxW(nullptr,widen(message).c_str(),L"Rage Wars Recompiled",MB_OK|MB_ICONERROR);}

}
