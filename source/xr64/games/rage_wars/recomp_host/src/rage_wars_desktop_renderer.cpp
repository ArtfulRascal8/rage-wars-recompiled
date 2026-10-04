#include "xr64_fast3d/n64_homogeneous_clip.hpp"
#include "xr64_fast3d/render_diagnostics.hpp"
#include "rage_wars_desktop_renderer.hpp"
#include "rage_wars_presentation_clock.hpp"
#include "rage_wars_presentation_policy.hpp"
#include "rage_wars_local_presentation.hpp"
#include "rage_wars_audio.hpp"
#include "rage_wars_audio_telemetry.hpp"
#include "rage_wars_preview_weapon.hpp"
#include "rage_wars_port_options.hpp"
#include "rage_wars_startup_transition.hpp"
#ifdef XR64_OPENXR
#include "rage_wars_openxr.hpp"
#include "rage_wars_xr_controls.hpp"
#include "rage_wars_xr_camera.hpp"
#include "rage_wars_xr_menu.hpp"
#include "rage_wars_xr_hand_ray.hpp"
#endif

#include "rage_wars_controller_mapping.hpp"
#include "rage_wars_graphics_bridge.hpp"
#include "rdram_profile.hpp"
#include "xr64_fast3d/n64_raw_fast3d_renderer.hpp"
#include "xr64_fast3d/n64_decoded_frame.hpp"
#include "xr64_fast3d/n64_frame_mailbox.hpp"
#include <thread>

#include <SDL.h>
#include <SDL_opengl.h>
#include <ultramodern/ultra64.h>
#include <ultramodern/renderer_context.hpp>
#include <Windows.h>
#pragma comment(lib, "Advapi32.lib")
#include <TlHelp32.h>
#include <cwctype>
#include <cctype>
#include <iterator>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" void xr64_body_request_toggle();

static std::atomic<bool> reversal_request{false};
static std::atomic<int> reversal_state{0};
extern "C" int xr64_damage_reversal_take_request() {
    return reversal_request.exchange(false, std::memory_order_relaxed) ? 1 : 0;
}
extern "C" void xr64_damage_reversal_observe(int state) {
    const int old = reversal_state.exchange(state, std::memory_order_relaxed);
    if (old != state) std::fprintf(stderr, "RW095_DAMAGE_REVERSAL state=%d (0=off,1=on,2=refused)\n", state);
}
static std::atomic<bool> rw092_ai_freeze{false};
extern "C" int xr64_rw092_ai_freeze_enabled() {
    return rw092_ai_freeze.load(std::memory_order_relaxed) ? 1 : 0;
}

namespace xr64::rage_wars::recomp {
namespace {
HostPresentationClock g_presentation_clock;
#ifdef XR64_OPENXR
RageWarsOpenXr g_xr;
XrMotionMapper g_motion_mapper;
std::atomic<bool> g_xr_hand_motion_active{false};
::xr64::N64FrameMailboxOf<LocalPlayerPresentation> g_xr_frames;
std::uint64_t g_last_presented_generation = 0;
std::uint64_t g_xr_presentations = 0;
double g_upload_cpu_ms = 0;
std::uint64_t g_upload_calls = 0;
const XrEyeFrame* g_xr_eye=nullptr;
bool g_xr_menu_flat=false;
GLuint g_xr_panel_texture=0;
std::atomic<bool> g_xr_active{false};
std::atomic<XrPresentationMode> g_xr_mode{XrPresentationMode::Desktop};
std::atomic<int> g_xr_queued_request{0}; // 0 none, 1 enter, 2 return; latest request wins.
std::atomic<std::uint64_t> g_xr_transition_revision{0};
std::mutex g_xr_status_mutex;
std::string g_xr_transition_status="Desktop";
bool g_xr_context_capable=true;
std::chrono::steady_clock::time_point g_xr_session_deadline{};
bool g_independent_policy_initialized=false;
bool g_independent_presentation=false;
bool xr_requested() { const char* v=std::getenv("XR64_OPENXR"); return v&&std::string(v)=="1"; }
bool independent_requested() { const char* v=std::getenv("XR64_XR_PRESENTATION"); return v&&std::string(v)=="independent"; }
bool xr_enabled() { return g_xr_active; }
bool xr_hand_motion_enabled() { return g_xr_hand_motion_active.load(std::memory_order_acquire); }
std::wstring selected_runtime_manifest() {
    wchar_t override_path[32768]{};
    SetLastError(ERROR_SUCCESS);
    const DWORD override_length=GetEnvironmentVariableW(L"XR_RUNTIME_JSON",override_path,
            static_cast<DWORD>(std::size(override_path)));
    if(override_length>0 && override_length<std::size(override_path))return override_path;
    if(GetLastError()!=ERROR_ENVVAR_NOT_FOUND && override_length==0)return {};
    for(HKEY root : {HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE}) {
        HKEY key=nullptr;
        if(RegOpenKeyExW(root,L"SOFTWARE\\Khronos\\OpenXR\\1",0,
                KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS)continue;
        wchar_t value[32768]{};DWORD type=0;DWORD bytes=sizeof(value);
        const LONG result=RegQueryValueExW(key,L"ActiveRuntime",nullptr,&type,
                reinterpret_cast<BYTE*>(value),&bytes);
        RegCloseKey(key);
        if(result==ERROR_SUCCESS && type==REG_SZ && value[0]!=L'\0')return value;
        // A user-specific selection has precedence; an unreadable one is unknown.
        if(root==HKEY_CURRENT_USER && result==ERROR_SUCCESS)return {};
    }
    return {};
}
bool passive_steamvr_ready_hint(std::string& reason) {
    const auto manifest_path=selected_runtime_manifest();
    if(manifest_path.empty()) { reason="No selected OpenXR runtime manifest could be read"; return false; }
    std::ifstream manifest(std::filesystem::path(manifest_path),std::ios::binary);
    if(!manifest) { reason="Selected OpenXR runtime manifest is unavailable"; return false; }
    std::string contents((std::istreambuf_iterator<char>(manifest)),{});
    std::wstring identity=manifest_path;
    const auto narrow=[](const std::wstring& value) {
        std::string out;out.reserve(value.size());
        for(wchar_t c:value)out.push_back(static_cast<char>(std::towlower(c)));
        return out;
    };
    std::string runtime=narrow(identity)+contents;
    std::transform(runtime.begin(),runtime.end(),runtime.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    const bool steamvr=runtime.find("steamvr")!=std::string::npos || runtime.find("steamxr")!=std::string::npos;
    const bool virtual_desktop=runtime.find("virtualdesktop")!=std::string::npos ||
            runtime.find("virtual desktop")!=std::string::npos || runtime.find("virtual_desktop")!=std::string::npos;
    if(!steamvr && !virtual_desktop) {
        reason="Selected runtime is not a recognized SteamVR or Virtual Desktop manifest; AUTO remains on desktop"; return false;
    }
    const wchar_t* expected_process=steamvr?L"vrserver.exe":L"virtualdesktop.streamer.exe";
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) { reason="Selected OpenXR provider process state is unreadable; AUTO remains on desktop"; return false; }
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    bool server_running=false;
    if(Process32FirstW(snapshot,&entry))do {
        std::wstring name=entry.szExeFile;
        std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        if(name==expected_process) {server_running=true;break;}
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    if(!server_running) {
        reason=steamvr?"SteamVR is selected but vrserver.exe is not running; AUTO remains on desktop":
                "Virtual Desktop is selected but VirtualDesktop.Streamer.exe is not running; AUTO remains on desktop";
        return false;
    }
    reason=steamvr?"SteamVR manifest is selected and vrserver.exe is running; guarded OpenXR initialization is permitted":
            "Virtual Desktop manifest is selected and VirtualDesktop.Streamer.exe is running; guarded OpenXR initialization is permitted";
    return true;
}
void set_xr_transition_status(XrPresentationMode mode,const std::string& status) {
    std::lock_guard lock(g_xr_status_mutex);
    const bool changed=g_xr_mode.load(std::memory_order_relaxed)!=mode || g_xr_transition_status!=status;
    g_xr_mode.store(mode,std::memory_order_release);
    g_xr_transition_status=status;
    if(changed)g_xr_transition_revision.fetch_add(1,std::memory_order_release);
}
void publish_xr_transition_request(XrTransitionRequest request) {
    const int value=request==XrTransitionRequest::EnterVR?1:2;
    g_xr_queued_request.store(value,std::memory_order_release);
    set_xr_transition_status(g_xr_mode.load(std::memory_order_acquire),
            request==XrTransitionRequest::EnterVR?"Enter VR queued":"Return to PC queued");
}
void detach_xr_to_desktop(const std::string& reason) {
    const auto mode=g_xr_mode.load(std::memory_order_acquire);
    if(mode!=XrPresentationMode::Desktop)
        set_xr_transition_status(XrPresentationMode::StoppingXR,"Stopping XR");
    g_xr_active.store(false,std::memory_order_release);
    g_xr_session_deadline={};
    g_xr.shutdown(); // publishes neutral XR input and destroys XR children on the GL owner.
    set_xr_transition_status(XrPresentationMode::Desktop,reason);
}
bool start_xr_on_owner(std::string& error) {
    if(g_xr_mode.load(std::memory_order_acquire)==XrPresentationMode::XRRunning)return true;
    set_xr_transition_status(XrPresentationMode::StartingXR,"Starting XR");
    if(!g_xr_context_capable)error="OpenGL fallback context does not meet XR graphics requirements";
    else if(g_xr.initialize(error)) {
        g_xr_active.store(true,std::memory_order_release);
        if(g_xr.session_running()) {
            g_xr_session_deadline={};
            const auto status=g_xr.session_focused()?"VR active; headset focused":"VR active; waiting for headset focus";
            set_xr_transition_status(XrPresentationMode::XRRunning,status);
        } else {
            g_xr_session_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            set_xr_transition_status(XrPresentationMode::StartingXR,"Starting VR; waiting for session readiness");
        }
        return true;
    }
    g_xr.shutdown();
    g_xr_session_deadline={};
    g_xr_active.store(false,std::memory_order_release);
    set_xr_transition_status(XrPresentationMode::Desktop,"VR unavailable: "+error);
    std::fprintf(stderr,"RW_XR_MODE active=desktop reason=%s\n",error.c_str());
    return false;
}void apply_xr_transition_request_on_owner() {
    const int request=g_xr_queued_request.exchange(0,std::memory_order_acq_rel);
    if(request==1) {
        std::string error;
        (void)start_xr_on_owner(error);
    } else if(request==2) {
        if(g_xr_mode.load(std::memory_order_acquire)==XrPresentationMode::Desktop) {
            set_xr_transition_status(XrPresentationMode::Desktop,"Desktop active");
        } else {
            detach_xr_to_desktop("Returned to PC");
        }
    }
}
bool xr_hand_ray_requested() {
    static const bool enabled=[] { const char* value=std::getenv("XR64_XR_HAND_RAY"); return value&&std::string(value)=="1"; }();
    return enabled;
}
void draw_xr_hand_ray(const XrEyeFrame& eye,const XrMotionInput& motion) {
    if(!xr_hand_ray_requested()||!motion.focused)return;
    XrTrackedRay ray;
    if(!xr_tracked_aim_ray(motion.right,ray))return;
    const auto point=[&](float metres) {
        return std::array<float,3>{ray.origin[0]+ray.forward[0]*metres,
                ray.origin[1]+ray.forward[1]*metres,ray.origin[2]+ray.forward[2]*metres};
    };
    const auto near_point=xr_project_tracked_point(point(0.08F),eye);
    const auto far_point=xr_project_tracked_point(point(3.0F),eye);
    if(!std::isfinite(near_point.w)||!std::isfinite(far_point.w)||
            near_point.w<0.02F||far_point.w<0.02F)return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_TEXTURE_2D);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glLineWidth(3.0F);glColor4f(0.10F,1.0F,0.90F,0.80F);
    glBegin(GL_LINES);
    glVertex3f(near_point.x/near_point.w,near_point.y/near_point.w,near_point.z/near_point.w);
    glVertex3f(far_point.x/far_point.w,far_point.y/far_point.w,far_point.z/far_point.w);
    glEnd();
    glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glPopAttrib();
}
#else
bool xr_requested() { return false; }
bool xr_enabled() { return false; }
bool xr_hand_motion_enabled() { return false; }
#endif
#if XR64_RENDER_DIAGNOSTICS
bool g_diagnostic_hud = true;
#else
bool g_diagnostic_hud = false;
#endif
bool g_bypass_mesh_shading = false;
bool g_widescreen = true;
float g_diagnostic_fov = 0.0F; // Zero preserves the game projection.
std::uint64_t g_port_options_revision = 0;
std::atomic<bool> g_capture_next_task{false};
std::atomic<bool> g_capture_next_xr_frame{false};
std::atomic<bool> g_capture_next_memory_task{false};
std::atomic<std::uint64_t> g_xr_capture_count{0};
std::uint64_t g_gl_error_count = 0;
std::uint64_t g_framebuffer_reads = 0;
GLuint g_hud_font = 0;

struct SdlApi {
    HMODULE module = nullptr;
    int (*init)(Uint32) = nullptr;
    void (*quit)() = nullptr;
    const char *(*error)() = nullptr;
    int (*poll_event)(SDL_Event *) = nullptr;
    const Uint8 *(*get_keyboard_state)(int *) = nullptr;
    Uint32 (*get_relative_mouse_state)(int *, int *) = nullptr;
    int (*set_relative_mouse_mode)(SDL_bool) = nullptr;
    int (*num_joysticks)() = nullptr;
    SDL_bool (*is_game_controller)(int) = nullptr;
    const char *(*joystick_name_for_index)(int) = nullptr;
    const char *(*game_controller_name_for_index)(int) = nullptr;
    SDL_GameController *(*game_controller_open)(int) = nullptr;
    void (*game_controller_close)(SDL_GameController *) = nullptr;
    SDL_bool (*game_controller_get_attached)(SDL_GameController *) = nullptr;
    const char *(*game_controller_name)(SDL_GameController *) = nullptr;
    SDL_Joystick *(*game_controller_get_joystick)(SDL_GameController *) = nullptr;
    SDL_JoystickID (*joystick_instance_id)(SDL_Joystick *) = nullptr;
    Uint8 (*game_controller_get_button)(SDL_GameController *, SDL_GameControllerButton) = nullptr;
    Sint16 (*game_controller_get_axis)(SDL_GameController *, SDL_GameControllerAxis) = nullptr;
    void (*game_controller_update)() = nullptr;
    void (*set_window_size)(SDL_Window *, int, int) = nullptr;
    int (*set_window_fullscreen)(SDL_Window *, Uint32) = nullptr;
    Uint32 (*get_window_flags)(SDL_Window *) = nullptr;
    int (*get_window_display_index)(SDL_Window *) = nullptr;
    int (*get_current_display_mode)(int,SDL_DisplayMode *) = nullptr;
    SDL_Window *(*create_window)(const char *, int, int, int, int, Uint32) = nullptr;
    void (*destroy_window)(SDL_Window *) = nullptr;
    int (*gl_set_attribute)(SDL_GLattr, int) = nullptr;
    SDL_GLContext (*gl_create_context)(SDL_Window *) = nullptr;
    int (*gl_make_current)(SDL_Window *, SDL_GLContext) = nullptr;
    void *(*gl_get_proc_address)(const char *) = nullptr;
    int (*gl_set_swap_interval)(int) = nullptr;
    void (*gl_swap_window)(SDL_Window *) = nullptr;
    void (*gl_delete_context)(SDL_GLContext) = nullptr;
    void (*get_window_size)(SDL_Window *, int *, int *) = nullptr;
    void (*gl_get_drawable_size)(SDL_Window *, int *, int *) = nullptr;
};

SdlApi g_sdl;

bool load_sdl(std::string &error) {
    if (g_sdl.module != nullptr) return true;
    g_sdl.module = LoadLibraryW(L"SDL2.dll");
    if (g_sdl.module == nullptr) {
        error = "SDL2.dll could not be loaded";
        return false;
    }
#define XR64_SDL_SYMBOL(field, exported) \
    g_sdl.field = reinterpret_cast<decltype(g_sdl.field)>(GetProcAddress(g_sdl.module, exported)); \
    if (g_sdl.field == nullptr) { error = std::string("SDL2 missing ") + exported; return false; }
    XR64_SDL_SYMBOL(init, "SDL_Init")
    XR64_SDL_SYMBOL(quit, "SDL_Quit")
    XR64_SDL_SYMBOL(error, "SDL_GetError")
    XR64_SDL_SYMBOL(poll_event, "SDL_PollEvent")
    XR64_SDL_SYMBOL(get_keyboard_state, "SDL_GetKeyboardState")
    XR64_SDL_SYMBOL(get_relative_mouse_state, "SDL_GetRelativeMouseState")
    XR64_SDL_SYMBOL(set_relative_mouse_mode, "SDL_SetRelativeMouseMode")
    XR64_SDL_SYMBOL(num_joysticks, "SDL_NumJoysticks")
    XR64_SDL_SYMBOL(is_game_controller, "SDL_IsGameController")
    XR64_SDL_SYMBOL(joystick_name_for_index, "SDL_JoystickNameForIndex")
    XR64_SDL_SYMBOL(game_controller_name_for_index, "SDL_GameControllerNameForIndex")
    XR64_SDL_SYMBOL(game_controller_open, "SDL_GameControllerOpen")
    XR64_SDL_SYMBOL(game_controller_close, "SDL_GameControllerClose")
    XR64_SDL_SYMBOL(game_controller_get_attached, "SDL_GameControllerGetAttached")
    XR64_SDL_SYMBOL(game_controller_name, "SDL_GameControllerName")
    XR64_SDL_SYMBOL(game_controller_get_joystick, "SDL_GameControllerGetJoystick")
    XR64_SDL_SYMBOL(joystick_instance_id, "SDL_JoystickInstanceID")
    XR64_SDL_SYMBOL(game_controller_get_button, "SDL_GameControllerGetButton")
    XR64_SDL_SYMBOL(game_controller_get_axis, "SDL_GameControllerGetAxis")
    XR64_SDL_SYMBOL(game_controller_update, "SDL_GameControllerUpdate")
    XR64_SDL_SYMBOL(create_window, "SDL_CreateWindow")
    XR64_SDL_SYMBOL(set_window_size, "SDL_SetWindowSize")
    XR64_SDL_SYMBOL(set_window_fullscreen, "SDL_SetWindowFullscreen")
    XR64_SDL_SYMBOL(get_window_flags, "SDL_GetWindowFlags")
    XR64_SDL_SYMBOL(get_window_display_index, "SDL_GetWindowDisplayIndex")
    XR64_SDL_SYMBOL(get_current_display_mode, "SDL_GetCurrentDisplayMode")
    XR64_SDL_SYMBOL(destroy_window, "SDL_DestroyWindow")
    XR64_SDL_SYMBOL(gl_set_attribute, "SDL_GL_SetAttribute")
    XR64_SDL_SYMBOL(gl_create_context, "SDL_GL_CreateContext")
    XR64_SDL_SYMBOL(gl_make_current, "SDL_GL_MakeCurrent")
    XR64_SDL_SYMBOL(gl_get_proc_address, "SDL_GL_GetProcAddress")
    XR64_SDL_SYMBOL(gl_set_swap_interval, "SDL_GL_SetSwapInterval")
    XR64_SDL_SYMBOL(gl_swap_window, "SDL_GL_SwapWindow")
    XR64_SDL_SYMBOL(gl_delete_context, "SDL_GL_DeleteContext")
    XR64_SDL_SYMBOL(get_window_size, "SDL_GetWindowSize")
    XR64_SDL_SYMBOL(gl_get_drawable_size, "SDL_GL_GetDrawableSize")
#undef XR64_SDL_SYMBOL
    return true;
}

const char *sdl_error() { return g_sdl.error != nullptr ? g_sdl.error() : "SDL2 error unavailable"; }

int g_present_swap_interval = -1;
DesktopPacingPolicy g_desktop_pacing;
std::chrono::steady_clock::time_point g_display_checked{};
void refresh_desktop_cadence(SDL_Window* window) {
    const auto now=std::chrono::steady_clock::now();
    if(g_display_checked!=std::chrono::steady_clock::time_point{} &&
        now-g_display_checked<std::chrono::seconds(1))return;
    g_display_checked=now;SDL_DisplayMode mode{};
    const int display=g_sdl.get_window_display_index(window);
    if(display>=0 && g_sdl.get_current_display_mode(display,&mode)==0)
        g_desktop_pacing.refresh(mode.refresh_rate);
}
void finish_desktop_cadence(std::chrono::steady_clock::time_point begin) {
    const auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
    g_desktop_pacing.frame(duration,g_present_swap_interval==1);
    if(g_desktop_pacing.software)std::this_thread::sleep_until(begin+std::chrono::nanoseconds(g_desktop_pacing.period_ns()));
}
void publish_local_control_state(std::uint64_t generation,const LocalCameraCorrection& local,const XrMotionInput& motion={}) {
    if(!local_state_sample_enabled())return;
    static LocalControlTelemetry telemetry;static auto reported=std::chrono::steady_clock::now();
    static std::uint64_t presentations=0;++presentations;
    const auto now=std::chrono::steady_clock::now();if(now-reported<std::chrono::seconds(1))return;
    reported=now;const auto state=local_presentation().acquire();const auto& p=state.player;
    char text[2048]{};
    const int bytes=std::snprintf(text,sizeof(text),
        "{\"schema\":1,\"ns\":%lld,\"generation\":%llu,\"presentations\":%llu,\"begins\":%llu,\"completed\":%llu,\"period_ns\":%lld,\"work_ns\":%lld,\"player_ns\":%lld,\"actor\":%u,\"valid\":%d,\"continuity\":%llu,\"input_epoch\":%llu,\"input\":%llu,\"applied\":%llu,\"yaw_total\":%.9f,\"pitch_total\":%.9f,\"yaw\":%.9f,\"pitch\":%.9f,\"render_player_update\":%llu,\"render_input\":%llu,\"late_yaw\":%.9f,\"late_pitch\":%.9f,\"render_valid\":%d,\"up\":[%.7f,%.7f,%.7f],\"player_origin\":[%.5f,%.5f,%.5f],\"render_origin\":[%.5f,%.5f,%.5f],\"checks\":%llu,\"errors\":%llu,\"xr_sequence\":%llu,\"xr_space_epoch\":%llu,\"xr_target_time\":%lld,\"xr_pose_ns\":%lld,\"xr_head_valid\":%d,\"xr_hand_valid\":%d,\"xr_head\":[%.5f,%.5f,%.5f],\"xr_grip\":[%.5f,%.5f,%.5f]}",
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(),generation,presentations,
        state.update_begins,p.update,p.update_period_ns,p.update_work_ns,p.sampled_ns,p.actor,int(p.valid),p.continuity,
        state.mouse.epoch,state.mouse.sequence,p.mouse.sequence,state.mouse.yaw,state.mouse.pitch,p.yaw,p.pitch,local.player_update,
        local.input_sequence,local.yaw,local.pitch,int(local.valid),local.target[4],local.target[5],local.target[6],
        p.origin[0],p.origin[1],p.origin[2],local.target[12],local.target[13],local.target[14],state.look_checks,state.look_errors,
        motion.sequence,motion.space_epoch,motion.pose_target_time,motion.host_monotonic_ns,int(motion.head_pose_valid),int(motion.right.pose_valid && motion.right.aim_pose_valid),
        motion.head_position[0],motion.head_position[1],motion.head_position[2],motion.right.position[0],motion.right.position[1],motion.right.position[2]);
    if(bytes>0)telemetry.publish(text,std::size_t(bytes));
}
void ensure_present_swap_interval(int interval) {
    if (g_present_swap_interval == interval) return;
    if (g_sdl.gl_set_swap_interval(interval) == 0) {
        g_present_swap_interval = interval;
    } else {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(stderr, "RW_PRESENT_SWAP_INTERVAL requested=%d failed=%s\n",
                    interval, sdl_error());
        }
    }
}

// Lives on the GL context owner, across task backends and desktop/XR transitions.
// Pixel bytes are retained for collision checks; the budget accounts for both
// that CPU copy and the corresponding RGBA GPU storage.
class SharedGlTextureCache {
    struct Entry {
        std::uint64_t hash = 0;
        ::xr64::N64RawFast3DTexture texture;
        GLuint id = 0;
        std::size_t charge = 0;
        std::uint64_t used = 0;
    };
    static constexpr std::size_t budget_ = 64U * 1024U * 1024U;
    std::vector<Entry> entries_;
    std::shared_ptr<const ::xr64::N64FrameSnapshotOf<LocalPlayerPresentation>> snapshot_;
    std::unordered_map<const ::xr64::N64RawFast3DTexture*, GLuint> snapshot_textures_;
    std::size_t bytes_ = 0;
    std::uint64_t clock_ = 0;
    std::uint64_t hits_ = 0, misses_ = 0, evictions_ = 0;

    static bool same(const ::xr64::N64RawFast3DTexture& a,
            const ::xr64::N64RawFast3DTexture& b) {
        return a.width == b.width && a.height == b.height &&
                a.format == b.format && a.size == b.size &&
                a.wrap_s == b.wrap_s && a.wrap_t == b.wrap_t &&
                a.alpha_precombined == b.alpha_precombined && a.rgba == b.rgba;
    }
    static GLint wrap(::xr64::N64RawTextureWrap mode) {
        if (mode == ::xr64::N64RawTextureWrap::Repeat) return GL_REPEAT;
        if (mode == ::xr64::N64RawTextureWrap::Mirror) return 0x8370;
        return GL_CLAMP;
    }
    void evict_one() {
        const auto least = std::min_element(entries_.begin(), entries_.end(),
                [](const Entry& a, const Entry& b) { return a.used < b.used; });
        if (least == entries_.end()) return;
        for (auto memo = snapshot_textures_.begin(); memo != snapshot_textures_.end();) {
            if (memo->second == least->id) memo = snapshot_textures_.erase(memo);
            else ++memo;
        }
        glDeleteTextures(1, &least->id);
        bytes_ -= least->charge;
        entries_.erase(least);
        ++evictions_;
    }
public:
    struct Result { GLuint id; bool uploaded; bool retained; };
    void bind_snapshot(std::shared_ptr<const ::xr64::N64FrameSnapshotOf<LocalPlayerPresentation>> snapshot) {
        if (snapshot_.get() == snapshot.get()) return;
        snapshot_textures_.clear();
        snapshot_ = std::move(snapshot);
    }
    Result acquire(const ::xr64::N64RawFast3DTexture& texture, bool immutable_payload) {
        if (immutable_payload) {
            const auto memo = snapshot_textures_.find(&texture);
            if (memo != snapshot_textures_.end()) {
                ++hits_;
                glBindTexture(GL_TEXTURE_2D, memo->second);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                return {memo->second, false, true};
            }
        }
        const std::uint64_t hash = texture.content_hash_valid ? texture.content_hash :
                ::xr64::n64_raw_fast3d_texture_hash(texture);
        const std::uint64_t now = ++clock_;
        for (auto& entry : entries_) {
            if (entry.hash == hash && same(entry.texture, texture)) {
                entry.used = now;
                ++hits_;
                glBindTexture(GL_TEXTURE_2D, entry.id);
                if (immutable_payload) snapshot_textures_[&texture] = entry.id;
                // Draw materials can change filtering on this texture. Restore
                // the upload/default state before each new texture operation.
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                return {entry.id, false, true};
            }
        }
        ++misses_;
        const std::size_t charge = texture.rgba.size() < budget_ / 2U ?
                texture.rgba.size() * 2U + sizeof(Entry) : budget_ + 1U;
        if (charge <= budget_) {
            while (bytes_ > budget_ - charge && !entries_.empty()) evict_one();
        }
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(texture.wrap_s));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(texture.wrap_t));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                static_cast<GLsizei>(texture.width),
                static_cast<GLsizei>(texture.height), 0, GL_RGBA,
                GL_UNSIGNED_BYTE, texture.rgba.data());
        if (charge <= budget_) {
            entries_.push_back({hash, texture, id, charge, now});
            bytes_ += charge;
            if (immutable_payload) snapshot_textures_[&texture] = id;
            return {id, true, true};
        }
        // Oversized resources are owned temporarily by the current backend.
        return {id, true, false};
    }
    void clear() {
        for (const auto& entry : entries_) glDeleteTextures(1, &entry.id);
        entries_.clear(); snapshot_textures_.clear(); snapshot_.reset();
        bytes_ = 0; clock_ = 0;
    }
    std::size_t bytes() const { return bytes_; }
    std::uint64_t hits() const { return hits_; }
    std::uint64_t misses() const { return misses_; }
    std::uint64_t evictions() const { return evictions_; }
};
SharedGlTextureCache g_texture_cache;
// One bounded streaming VBO, used only on the renderer's current GL context.
// The legacy immediate path remains available for diagnostics and capability
// fallback. At most 32 MiB is staged/uploaded by one triangle batch.
constexpr GLenum kGlArrayBuffer = 0x8892;
constexpr GLenum kGlArrayBufferBinding = 0x8894;
constexpr GLenum kGlStreamDraw = 0x88E0;
constexpr std::size_t kTriangleVboByteLimit = 32U * 1024U * 1024U;
struct TriangleArrayVertex {
    GLfloat position[3];
    GLfloat texcoord[4];
    GLubyte color[4];
};
struct TriangleVboPath {
    using GenBuffersProc = void (APIENTRY *)(GLsizei, GLuint *);
    using DeleteBuffersProc = void (APIENTRY *)(GLsizei, const GLuint *);
    using BindBufferProc = void (APIENTRY *)(GLenum, GLuint);
    using BufferDataProc = void (APIENTRY *)(GLenum, std::ptrdiff_t, const void *, GLenum);
    GenBuffersProc gen_buffers = nullptr;
    DeleteBuffersProc delete_buffers = nullptr;
    BindBufferProc bind_buffer = nullptr;
    BufferDataProc buffer_data = nullptr;
    HGLRC owner_context = nullptr;
    GLuint buffer = 0;
    bool initialized = false;
    bool ready = false;
    const char *reason = "not_initialized";
    std::vector<TriangleArrayVertex> staging;
    std::uint64_t vbo_draws = 0;
    std::uint64_t stream_draws = 0;
    std::uint64_t stream_upload_bytes = 0;
    std::uint64_t immediate_fallbacks = 0;
    bool fallback_reported = false;

    template <typename Proc>
    Proc load_proc(const char *core_name, const char *arb_name) const {
        if (!g_sdl.gl_get_proc_address) return nullptr;
        auto proc = reinterpret_cast<Proc>(g_sdl.gl_get_proc_address(core_name));
        if (!proc && arb_name) proc = reinterpret_cast<Proc>(g_sdl.gl_get_proc_address(arb_name));
        return proc;
    }
    static bool has_extension(const char *extensions, const char *wanted) {
        if (!extensions || !wanted || !*wanted) return false;
        const std::size_t length = std::strlen(wanted);
        for (const char *at = extensions; (at = std::strstr(at, wanted)) != nullptr; ++at) {
            const bool left = at == extensions || at[-1] == ' ';
            const char right_char = at[length];
            if (left && (right_char == '\0' || right_char == ' ')) return true;
        }
        return false;
    }
    bool ensure() {
        const HGLRC current = wglGetCurrentContext();
        if (!current) { reason = "no_current_gl_context"; return false; }
        if (initialized) {
            if (owner_context != current) { ready = false; reason = "context_changed_without_shutdown"; return false; }
            return ready;
        }
        initialized = true;
        owner_context = current;
        int major = 0, minor = 0;
        const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        if (version) std::sscanf(version, "%d.%d", &major, &minor);
        const bool core_vbo = major > 1 || (major == 1 && minor >= 5);
        const auto *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
        if (!core_vbo && !has_extension(extensions, "GL_ARB_vertex_buffer_object")) {
            reason = "vbo_not_supported";
            return false;
        }
        gen_buffers = load_proc<GenBuffersProc>("glGenBuffers", "glGenBuffersARB");
        delete_buffers = load_proc<DeleteBuffersProc>("glDeleteBuffers", "glDeleteBuffersARB");
        bind_buffer = load_proc<BindBufferProc>("glBindBuffer", "glBindBufferARB");
        buffer_data = load_proc<BufferDataProc>("glBufferData", "glBufferDataARB");
        if (!gen_buffers || !delete_buffers || !bind_buffer || !buffer_data) {
            reason = "vbo_entry_point_unavailable";
            return false;
        }
        gen_buffers(1, &buffer);
        if (!buffer) { reason = "vbo_allocation_failed"; return false; }
        ready = true;
        reason = nullptr;
        return true;
    }
    void release_current_context() {
        if (owner_context && owner_context == wglGetCurrentContext() && buffer && delete_buffers)
            delete_buffers(1, &buffer);
        buffer = 0;
        owner_context = nullptr;
        initialized = false;
        ready = false;
        reason = "not_initialized";
        gen_buffers = nullptr; delete_buffers = nullptr;
        bind_buffer = nullptr; buffer_data = nullptr;
        std::vector<TriangleArrayVertex>().swap(staging);
        vbo_draws = stream_draws = stream_upload_bytes = immediate_fallbacks = 0;
        fallback_reported = false;
    }
};
TriangleVboPath g_triangle_vbo;
#ifdef XR64_OPENXR
// A single raw vertex arena is uploaded once for a decoded generation. Replay
// still visits every draw in its original order; each eye supplies its own pose
// and projection uniforms. The streaming path remains the capability fallback.
struct RetainedXrGeometryPath {
    using CreateShaderProc = GLuint (APIENTRY *)(GLenum);
    using ShaderSourceProc = void (APIENTRY *)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    using CompileShaderProc = void (APIENTRY *)(GLuint);
    using GetShaderivProc = void (APIENTRY *)(GLuint, GLenum, GLint*);
    using DeleteShaderProc = void (APIENTRY *)(GLuint);
    using CreateProgramProc = GLuint (APIENTRY *)();
    using AttachShaderProc = void (APIENTRY *)(GLuint, GLuint);
    using LinkProgramProc = void (APIENTRY *)(GLuint);
    using GetProgramivProc = void (APIENTRY *)(GLuint, GLenum, GLint*);
    using DeleteProgramProc = void (APIENTRY *)(GLuint);
    using UseProgramProc = void (APIENTRY *)(GLuint);
    using GetUniformLocationProc = GLint (APIENTRY *)(GLuint, const GLchar*);
    using Uniform1iProc = void (APIENTRY *)(GLint, GLint);
    using Uniform1fProc = void (APIENTRY *)(GLint, GLfloat);
    using Uniform2fProc = void (APIENTRY *)(GLint, GLfloat, GLfloat);
    using Uniform3fProc = void (APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat);
    using Uniform4fProc = void (APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    CreateShaderProc create_shader = nullptr;
    ShaderSourceProc shader_source = nullptr;
    CompileShaderProc compile_shader = nullptr;
    GetShaderivProc get_shader_iv = nullptr;
    DeleteShaderProc delete_shader = nullptr;
    CreateProgramProc create_program = nullptr;
    AttachShaderProc attach_shader = nullptr;
    LinkProgramProc link_program = nullptr;
    GetProgramivProc get_program_iv = nullptr;
    DeleteProgramProc delete_program = nullptr;
    UseProgramProc use_program = nullptr;
    GetUniformLocationProc get_uniform_location = nullptr;
    Uniform1iProc uniform_1i = nullptr;
    Uniform1fProc uniform_1f = nullptr;
    Uniform2fProc uniform_2f = nullptr;
    Uniform3fProc uniform_3f = nullptr;
    Uniform4fProc uniform_4f = nullptr;
    HGLRC owner_context = nullptr;
    GLuint buffer = 0, program = 0;
    const ::xr64::N64FrameSnapshotOf<LocalPlayerPresentation>* uploaded_snapshot = nullptr;
    std::uint64_t uploaded_generation = 0;
    bool initialized = false, ready = false;
    bool program_bound = false;
    bool client_scope_bound = false;
    bool texcoord_array_enabled = false;
    GLint incoming_array_buffer = 0;
    std::uint64_t binding_queries = 0, client_scope_starts = 0;
    GLuint incoming_program = 0;
    const char* reason = "not_initialized";
    std::uint64_t uploads = 0, upload_bytes = 0, draws = 0, reused_draws = 0;
    std::uint64_t fallbacks = 0;
    struct Uniforms {
        GLint norm_inv, eye_pos, orientation, projection, depth;
        GLint viewport, logical_inv, affine, bypass_shading;
        GLint material, texture, primitive_color, environment_color, alpha_scale;
    } u{};
    enum class FloatSlot : std::size_t {
        NormInv, EyePos, Orientation, Projection, Depth, Viewport,
        LogicalInv, PrimitiveColor, EnvironmentColor, AlphaScale, Count
    };
    enum class IntSlot : std::size_t { Affine, Material, Texture, BypassShading, Count };
    struct FloatUniformCache {
        std::array<GLfloat, 4> value{};
        bool valid = false;
    };
    struct IntUniformCache { GLint value = 0; bool valid = false; };
    std::array<FloatUniformCache, static_cast<std::size_t>(FloatSlot::Count)> float_cache{};
    std::array<IntUniformCache, static_cast<std::size_t>(IntSlot::Count)> int_cache{};
    std::uint64_t uniform_updates = 0, uniform_skips = 0;

    bool ensure() {
        const HGLRC current = wglGetCurrentContext();
        if (!current) { reason = "no_current_gl_context"; return false; }
        if (initialized) {
            if (owner_context != current) { reason = "context_changed_without_shutdown"; return false; }
            return ready;
        }
        initialized = true;
        owner_context = current;
        if (!g_triangle_vbo.ensure()) { reason = g_triangle_vbo.reason; return false; }
        int major = 0, minor = 0;
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if (version) std::sscanf(version, "%d.%d", &major, &minor);
        if (major < 2 || (major == 2 && minor < 1)) {
            reason = "glsl_120_unavailable"; return false;
        }
#define XR64_LOAD_SHADER_PROC(field, name) \
        field = g_triangle_vbo.load_proc<decltype(field)>(name, nullptr); \
        if (!field) { reason = "shader_entry_point_unavailable"; return false; }
        XR64_LOAD_SHADER_PROC(create_shader, "glCreateShader")
        XR64_LOAD_SHADER_PROC(shader_source, "glShaderSource")
        XR64_LOAD_SHADER_PROC(compile_shader, "glCompileShader")
        XR64_LOAD_SHADER_PROC(get_shader_iv, "glGetShaderiv")
        XR64_LOAD_SHADER_PROC(delete_shader, "glDeleteShader")
        XR64_LOAD_SHADER_PROC(create_program, "glCreateProgram")
        XR64_LOAD_SHADER_PROC(attach_shader, "glAttachShader")
        XR64_LOAD_SHADER_PROC(link_program, "glLinkProgram")
        XR64_LOAD_SHADER_PROC(get_program_iv, "glGetProgramiv")
        XR64_LOAD_SHADER_PROC(delete_program, "glDeleteProgram")
        XR64_LOAD_SHADER_PROC(use_program, "glUseProgram")
        XR64_LOAD_SHADER_PROC(get_uniform_location, "glGetUniformLocation")
        XR64_LOAD_SHADER_PROC(uniform_1i, "glUniform1i")
        XR64_LOAD_SHADER_PROC(uniform_1f, "glUniform1f")
        XR64_LOAD_SHADER_PROC(uniform_2f, "glUniform2f")
        XR64_LOAD_SHADER_PROC(uniform_3f, "glUniform3f")
        XR64_LOAD_SHADER_PROC(uniform_4f, "glUniform4f")
#undef XR64_LOAD_SHADER_PROC
        constexpr GLenum vertex_shader = 0x8B31, compile_status = 0x8B81;
        constexpr GLenum link_status = 0x8B82;
        static constexpr const char* vertex_source = R"GLSL(#version 120
uniform vec3 u_norm_inv;
uniform vec3 u_eye_pos;
uniform vec4 u_orientation;
uniform vec4 u_projection;
uniform vec2 u_depth;
uniform vec4 u_viewport;
uniform vec2 u_logical_inv;
uniform int u_affine;
varying vec4 v_color_num;
varying float v_color_den;
varying vec2 v_uv_num;
varying float v_uv_den;
void main() {
    vec3 p = vec3(gl_Vertex.x * u_norm_inv.x,
                  gl_Vertex.y * u_norm_inv.y,
                  -gl_Vertex.w * u_norm_inv.z) - u_eye_pos;
    vec3 t = 2.0 * cross(u_orientation.xyz, p);
    p += u_orientation.w * t + cross(u_orientation.xyz, t);
    vec4 clip = vec4(u_projection.x * p.x + u_projection.y * p.z,
                     u_projection.z * p.y + u_projection.w * p.z,
                     u_depth.x * p.z + u_depth.y, -p.z);
    gl_Position = vec4((clip.x * u_viewport.x + clip.w * u_viewport.z)
                           * u_logical_inv.x - clip.w,
                       clip.w - (clip.w * u_viewport.w - clip.y * u_viewport.y)
                           * u_logical_inv.y,
                       clip.z, clip.w);
    // The established pixel-space path interpolates vertex color linearly in
    // screen space. Multiply by clip W, then divide in the fragment shader.
    v_color_num = gl_Color * clip.w;
    v_color_den = clip.w;
    float uv_w = u_affine != 0 ? clip.w : 1.0;
    v_uv_num = gl_MultiTexCoord0.st * uv_w;
    v_uv_den = uv_w;
}
)GLSL";
        static constexpr const char* fragment_source = R"GLSL(#version 120
uniform sampler2D u_texture;
uniform int u_material;
uniform int u_bypass_shading;
uniform vec4 u_primitive_color;
uniform vec4 u_environment_color;
uniform float u_alpha_scale;
varying vec4 v_color_num;
varying float v_color_den;
varying vec2 v_uv_num;
varying float v_uv_den;
void main() {
    vec4 shade = v_color_num / v_color_den;
    if (u_bypass_shading != 0) shade.rgb = vec3(1.0);
    if (u_material == 0) {
        gl_FragColor = shade;
    } else {
        vec4 texel = texture2D(u_texture, v_uv_num / v_uv_den);
        if (u_material == 2)
            gl_FragColor = vec4(texel.rgb, texel.a * u_alpha_scale);
        else if (u_material == 3)
            gl_FragColor = vec4(mix(u_environment_color.rgb,
                    u_primitive_color.rgb, texel.rgb),
                    texel.a * u_primitive_color.a);
        else
            gl_FragColor = texel * shade;
    }
}
)GLSL";
        const auto compile = [&](GLenum kind, const char* shader_source) -> GLuint {
            const GLuint shader = create_shader(kind);
            if (!shader) return 0;
            this->shader_source(shader, 1, &shader_source, nullptr);
            compile_shader(shader);
            GLint compiled = 0;
            get_shader_iv(shader, compile_status, &compiled);
            if (compiled) return shader;
            delete_shader(shader);
            return 0;
        };
        const GLuint vertex = compile(vertex_shader, vertex_source);
        if (!vertex) { reason = "vertex_shader_compile_failed"; return false; }
        const GLuint fragment = compile(0x8B30 /* GL_FRAGMENT_SHADER */, fragment_source);
        if (!fragment) {
            delete_shader(vertex); reason = "fragment_shader_compile_failed"; return false;
        }
        program = create_program();
        if (!program) {
            delete_shader(vertex); delete_shader(fragment);
            reason = "shader_program_allocation_failed"; return false;
        }
        attach_shader(program, vertex);
        attach_shader(program, fragment);
        link_program(program);
        delete_shader(vertex);
        delete_shader(fragment);
        GLint linked = 0;
        get_program_iv(program, link_status, &linked);
        if (!linked) {
            delete_program(program); program = 0;
            reason = "shader_program_link_failed"; return false;
        }
        u.norm_inv = get_uniform_location(program, "u_norm_inv");
        u.eye_pos = get_uniform_location(program, "u_eye_pos");
        u.orientation = get_uniform_location(program, "u_orientation");
        u.projection = get_uniform_location(program, "u_projection");
        u.depth = get_uniform_location(program, "u_depth");
        u.viewport = get_uniform_location(program, "u_viewport");
        u.logical_inv = get_uniform_location(program, "u_logical_inv");
        u.affine = get_uniform_location(program, "u_affine");
        u.bypass_shading = get_uniform_location(program, "u_bypass_shading");
        u.material = get_uniform_location(program, "u_material");
        u.texture = get_uniform_location(program, "u_texture");
        u.primitive_color = get_uniform_location(program, "u_primitive_color");
        u.environment_color = get_uniform_location(program, "u_environment_color");
        u.alpha_scale = get_uniform_location(program, "u_alpha_scale");
        g_triangle_vbo.gen_buffers(1, &buffer);
        if (!buffer) { reason = "retained_buffer_allocation_failed"; return false; }
        ready = true;
        reason = nullptr;
        return true;
    }
    void bind_program() {
        if (program_bound) return;
        GLint current = 0;
        glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &current);
        incoming_program = static_cast<GLuint>(current);
        use_program(program);
        program_bound = true;
    }
    void restore_program() {
        if (!program_bound || owner_context != wglGetCurrentContext()) return;
        use_program(incoming_program);
        incoming_program = 0;
        program_bound = false;
    }
    void bind_draw_scope() {
        if (client_scope_bound) return;
        glGetIntegerv(kGlArrayBufferBinding, &incoming_array_buffer);
        ++binding_queries;
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        g_triangle_vbo.bind_buffer(kGlArrayBuffer, buffer);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(4, GL_FLOAT, sizeof(::xr64::N64RawFast3DVertex),
                reinterpret_cast<const GLvoid*>(offsetof(::xr64::N64RawFast3DVertex, x)));
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(::xr64::N64RawFast3DVertex),
                reinterpret_cast<const GLvoid*>(offsetof(::xr64::N64RawFast3DVertex, color)));
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        texcoord_array_enabled = false;
        client_scope_bound = true;
        ++client_scope_starts;
    }
    void set_texcoord_array(bool textured) {
        if (!client_scope_bound || texcoord_array_enabled == textured) return;
        if (textured) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, sizeof(::xr64::N64RawFast3DVertex),
                    reinterpret_cast<const GLvoid*>(offsetof(::xr64::N64RawFast3DVertex, s)));
        } else {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        texcoord_array_enabled = textured;
    }
    void restore_draw_scope() {
        if (client_scope_bound && owner_context == wglGetCurrentContext()) {
            glPopClientAttrib();
            g_triangle_vbo.bind_buffer(kGlArrayBuffer,
                    static_cast<GLuint>(incoming_array_buffer));
            incoming_array_buffer = 0;
            client_scope_bound = false;
            texcoord_array_enabled = false;
        }
        restore_program();
    }
    bool changed(FloatSlot slot, const std::array<GLfloat, 4>& value) {
        auto& entry = float_cache[static_cast<std::size_t>(slot)];
        if (entry.valid && entry.value == value) { ++uniform_skips; return false; }
        entry.value = value;
        entry.valid = true;
        ++uniform_updates;
        return true;
    }
    void send_1f(FloatSlot slot, GLint location, GLfloat x) {
        if (changed(slot, {x, 0.0F, 0.0F, 0.0F})) uniform_1f(location, x);
    }
    void send_2f(FloatSlot slot, GLint location, GLfloat x, GLfloat y) {
        if (changed(slot, {x, y, 0.0F, 0.0F})) uniform_2f(location, x, y);
    }
    void send_3f(FloatSlot slot, GLint location, GLfloat x, GLfloat y, GLfloat z) {
        if (changed(slot, {x, y, z, 0.0F})) uniform_3f(location, x, y, z);
    }
    void send_4f(FloatSlot slot, GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
        if (changed(slot, {x, y, z, w})) uniform_4f(location, x, y, z, w);
    }
    void send_1i(IntSlot slot, GLint location, GLint value) {
        auto& entry = int_cache[static_cast<std::size_t>(slot)];
        if (entry.valid && entry.value == value) { ++uniform_skips; return; }
        entry.value = value;
        entry.valid = true;
        ++uniform_updates;
        uniform_1i(location, value);
    }
    void write_metrics() const {
        const char* filename = std::getenv("XR64_XR_GEOMETRY_METRICS_PATH");
        if (!filename || !*filename) return;
        std::ofstream output(filename, std::ios::trunc);
        if (!output) return;
        output << "retained_ready=" << (ready ? 1 : 0) << '\n'
               << "retained_reason=" << (reason ? reason : "ready") << '\n'
               << "retained_uploads=" << uploads << '\n'
               << "retained_upload_bytes=" << upload_bytes << '\n'
               << "retained_draws=" << draws << '\n'
               << "retained_reused_draws=" << reused_draws << '\n'
               << "retained_fallbacks=" << fallbacks << '\n'
               << "retained_uniform_updates=" << uniform_updates << '\n'
               << "retained_uniform_skips=" << uniform_skips << '\n'
               << "retained_binding_queries=" << binding_queries << '\n'
               << "retained_client_scope_starts=" << client_scope_starts << '\n'
               << "stream_draws=" << g_triangle_vbo.stream_draws << '\n'
               << "stream_upload_bytes=" << g_triangle_vbo.stream_upload_bytes << '\n'
               << "immediate_draws=" << g_triangle_vbo.immediate_fallbacks << '\n';
    }
    void release_current_context() {
        restore_draw_scope();
        write_metrics();
        if (owner_context && owner_context == wglGetCurrentContext()) {
            if (buffer && g_triangle_vbo.delete_buffers) g_triangle_vbo.delete_buffers(1, &buffer);
            if (program && delete_program) delete_program(program);
        }
        *this = {};
    }
};
RetainedXrGeometryPath g_xr_retained_geometry;
#endif
struct PresentationPerfWindow {
    HostPresentationMode mode = HostPresentationMode::Desktop;
    bool have_mode = false;
    std::array<double, 300> intervals_ms{};
    std::array<double, 300> decoded_ages_ms{};
    std::size_t count = 0;
    std::size_t age_count = 0;
    std::uint64_t repeats = 0;
    void record(const HostPresentationTick& tick) {
        if (!have_mode || mode != tick.mode) {
            mode = tick.mode;
            have_mode = true;
            count = age_count = 0;
            repeats = 0;
        }
        intervals_ms[count++] =
                std::chrono::duration<double, std::milli>(tick.raw_elapsed).count();
        if (tick.decoded_age_known) {
            decoded_ages_ms[age_count++] =
                    std::chrono::duration<double, std::milli>(tick.decoded_age).count();
        }
        repeats += tick.repeated_generation ? 1U : 0U;
        if (count != intervals_ms.size()) return;
        auto intervals = intervals_ms;
        std::sort(intervals.begin(), intervals.end());
        auto ages = decoded_ages_ms;
        std::sort(ages.begin(), ages.begin() + age_count);
        const auto percentile = [](const auto& samples, std::size_t n, double p) {
            if (n == 0) return -1.0;
            const auto index = static_cast<std::size_t>(std::ceil(p * n)) - 1U;
            return samples[std::min(index, n - 1U)];
        };
        std::fprintf(stderr,
                "RW_PRESENT_TIMING mode=%s samples=%zu interval_median_ms=%.3f interval_p95_ms=%.3f decoded_age_median_ms=%.3f decoded_age_p95_ms=%.3f decoded_age_samples=%zu repeated=%llu\n",
                mode == HostPresentationMode::OpenXR ? "xr" : "desktop", count,
                percentile(intervals, count, 0.50), percentile(intervals, count, 0.95),
                percentile(ages, age_count, 0.50), percentile(ages, age_count, 0.95),
                age_count, static_cast<unsigned long long>(repeats));
        count = age_count = 0;
        repeats = 0;
    }
};
PresentationPerfWindow g_presentation_perf;
bool triangle_perf_enabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("XR64_RW_PERF");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}
#ifdef XR64_OPENXR
bool xr_triangle_vbo_enabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("XR64_XR_TRIANGLE_VBO");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}
bool xr_retained_geometry_enabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("XR64_XR_RETAINED_GEOMETRY");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled && xr_triangle_vbo_enabled();
}
#endif
void note_triangle_submission(bool vbo, const char *reason = nullptr) {
    if (vbo) {
        ++g_triangle_vbo.vbo_draws;
    } else {
        ++g_triangle_vbo.immediate_fallbacks;
        if (!g_triangle_vbo.fallback_reported) {
            g_triangle_vbo.fallback_reported = true;
            std::fprintf(stderr, "RW_TRIANGLE_VBO path=immediate reason=%s\n",
                    reason ? reason : "unspecified");
        }
    }
    const auto total = g_triangle_vbo.vbo_draws + g_triangle_vbo.immediate_fallbacks;
    if (triangle_perf_enabled() && total % 300U == 0U) {
        std::fprintf(stderr, "RW_TRIANGLE_VBO vbo_draws=%llu immediate_fallbacks=%llu\n",
                static_cast<unsigned long long>(g_triangle_vbo.vbo_draws),
                static_cast<unsigned long long>(g_triangle_vbo.immediate_fallbacks));
    }
}

class OpenGlFast3DBackend final :
        public ::xr64::rage_wars::IRageWarsRenderBackend,
        public ::xr64::rage_wars::IRageWarsRawRenderBackend,
        public ::xr64::N64RawFast3DBackend {
public:
    explicit OpenGlFast3DBackend(SDL_Window *window, std::uint64_t task_sequence,
            std::shared_ptr<const ::xr64::N64FrameSnapshotOf<LocalPlayerPresentation>> snapshot = {}) :
            window_(window), task_sequence_(task_sequence),
            immutable_payload_(static_cast<bool>(snapshot)),
            defer_presentation_(static_cast<bool>(snapshot)),
            frame_snapshot_(snapshot.get()),
            menu_panel_(snapshot ? snapshot->menu_panel : true),
            startup_(snapshot ? snapshot->startup : false) {
        // Independent task backends record commands on the producer thread.
        // Only the GL owner replay may mutate the shared texture cache.
#ifdef XR64_OPENXR
        if (!g_independent_presentation || immutable_payload_)
#endif
            g_texture_cache.bind_snapshot(std::move(snapshot));
    }

    bool submit_scene(const ::xr64::rage_wars::LiveN64TaskContext &,
            const ::xr64::RenderSceneData &scene, std::string &) override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        for (const ::xr64::ModelSurface &surface : scene.surfaces) {
            if (surface.alpha_blend) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            } else {
                glDisable(GL_BLEND);
            }
            glBegin(GL_TRIANGLES);
            for (const std::uint32_t index : surface.indices) {
                if (index >= surface.vertices.size()) continue;
                const ::xr64::ModelVertex &vertex = surface.vertices[index];
                glColor4f(vertex.color_r, vertex.color_g, vertex.color_b, vertex.color_a);
                if (vertex.fast3d_has_screen_xy_override) {
                    glVertex3f(
                            static_cast<float>(vertex.fast3d_screen_x_s13_2) / 640.0F - 1.0F,
                            1.0F - static_cast<float>(vertex.fast3d_screen_y_s13_2) / 480.0F,
                            0.0F);
                } else {
                    glVertex3f(vertex.position_x / 160.0F,
                            vertex.position_y / 120.0F, vertex.position_z / 65536.0F);
                }
            }
            glEnd();
        }
        g_sdl.gl_swap_window(window_);
        return true;
    }

    ~OpenGlFast3DBackend() override {
#ifdef XR64_OPENXR
        if (frame_snapshot_ &&
                g_xr_retained_geometry.owner_context == wglGetCurrentContext())
            g_xr_retained_geometry.restore_draw_scope();
#endif
        if (transient_texture_id_) glDeleteTextures(1, &transient_texture_id_);
    }
    ::xr64::N64DecodedFrame decoded_frame;
    const ::xr64::N64RawFast3DTaskStats& decoded_stats() const { return raw_stats_; }
    bool menu_panel() const { return menu_panel_; }
    bool startup() const { return startup_; }
    LocalPlayerPresentation scene_player() const { return scene_player_; }
    void local_camera(const LocalCameraCorrection& correction) { local_camera_=correction; }
    void presentation_motion(const XrMotionInput& motion) { presentation_motion_=motion; }
    bool present_mirror(const ::xr64::N64RawFast3DTaskStats& stats, bool repeated, std::string& error) {
        raw_stats_ = stats;
        suppress_capture_ = repeated;
        frame_begin_ = std::chrono::steady_clock::now();
        defer_presentation_ = false;
        return end_frame(error);
    }

    bool submit_raw_task(
            const ::xr64::rage_wars::LiveN64TaskContext &context,
            const ::xr64::N64GraphicsTaskDescriptor &task,
            std::string &error) override {
        menu_panel_ = !xr_base_gameplay(context.rdram);
        // Capture startup classification with this task, before decoding its commands.
        startup_ = startup_state(context.rdram, context.rdram_size);
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr,
                "RW020_F3DEX2_EXEC_BEGIN task_guest=0x%08X dl_physical=0x%08X "
                "rdram_size=%zu\n", context.rsp_task_address,
                task.task_data_address, context.rdram_size);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
#ifndef XR64_DEMO_BUILD
        // Optional one-task capture request, consumed on the graphics owner.
        // This permits capturing the owner's current menu without replay input.
        if (const char *directory = std::getenv("XR64_RW_CAPTURE_DIR")) {
            std::error_code ec;
            const bool full_task_capture = g_capture_next_task.exchange(false) ||
                    std::filesystem::remove(std::filesystem::path(directory) / "capture-next-task", ec);
            // XR F9 requests memory without enabling expensive per-draw diagnostics.
            const bool memory_task_capture = g_capture_next_memory_task.exchange(false);
            const auto sequence = std::to_string(task.sequence);
            if (full_task_capture) {
                ::xr64::render_diagnostics::capture_task.store(task.sequence, std::memory_order_relaxed);
                std::fprintf(stderr, "RW_CAPTURE_REQUEST task=%llu mode=one-task\n", static_cast<unsigned long long>(task.sequence));
                _putenv_s("XR64_RW_DIAG_TASK", sequence.c_str());
                _putenv_s("XR64_RW_CAPTURE_TASK", sequence.c_str());
            }
            const char *memory_capture = std::getenv("XR64_RW_MEMORY_CAPTURE");
            if ((full_task_capture || memory_task_capture) &&
                    memory_capture && std::string(memory_capture) == "1") {
                // Graphics-task-boundary sample of host-word-swapped RDRAM,
                // not an atomic save state. XR screenshot task can differ.
                std::filesystem::create_directories(directory, ec);
                const auto path = std::filesystem::path(directory) /
                        ("task-" + sequence + "-rdram-host.bin");
                std::ofstream snapshot(path, std::ios::binary);
                snapshot.write(reinterpret_cast<const char *>(context.rdram),
                        static_cast<std::streamsize>(context.rdram_size));
                snapshot.close();
                std::fprintf(stderr,
                        "RW090_MEMORY_CAPTURE task=%llu bytes=%zu layout=host-word-swapped byte_xor=3 atomic=0 saved=%d dl_physical=0x%08X source=%s\n",
                        static_cast<unsigned long long>(task.sequence),
                        context.rdram_size, snapshot.good() ? 1 : 0,
                        task.task_data_address, memory_task_capture ? "xr-f9" : "task");
            }
        }
#endif
        frame_begin_ = std::chrono::steady_clock::now();
        raw_stats_ = {};
        const auto preview_frame=gate5_preview_weapon_frame(task.task_data_address,task.task_data_size);
        scene_player_=preview_frame.player;
        auto preview_replacement=prepare_preview_weapon(context.rdram,
                preview_frame.actor,xr_enabled() && xr_hand_motion_enabled(),gate5_xr_motion_snapshot(),&preview_frame,
                port_options::snapshot().calibrated_weapon_models);
        if(preview_frame.actor && preview_frame.list) {
            preview_replacement.physical_address=preview_frame.list&0x7FFFFFU;
            preview_replacement.camera_attachment=true;
        }
#ifdef XR64_OPENXR
        const bool owner_replay = xr_enabled() || g_independent_presentation;
#else
        const bool owner_replay = false;
#endif
        const bool translated = ::xr64::execute_n64_raw_fast3d_task(task,
                context.rdram, context.rdram_size, owner_replay ? static_cast<::xr64::N64RawFast3DBackend&>(decoded_frame) : *this, error, &raw_stats_, &preview_replacement);
        if(xr_base_gameplay(context.rdram)) {
            static unsigned checked=0,matched=0,unprepared=0,missing=0,unrecorded=0;
            ++checked;
            if(checked<=12)std::fprintf(stderr,"RW118_PREVIEW_TASK task=%llu dl=%08X size=%u selected=%08X matched=%d\n",static_cast<unsigned long long>(task.sequence),task.task_data_address,task.task_data_size,preview_frame.list,preview_replacement.matched?1:0);
            if(!preview_frame.actor)++unrecorded;
            else if(preview_replacement.matched)++matched;
            else if(preview_replacement.batches.empty())++unprepared;
            else ++missing;
            if(checked%300==0)std::fprintf(stderr,"RW117_PREVIEW_COVERAGE checked=%u matched=%u unprepared=%u missing_list=%u unrecorded=%u task_dl=%08X size=%u\n",checked,matched,unprepared,missing,unrecorded,task.task_data_address,task.task_data_size);
        }
        if(preview_replacement.matched) {
            static unsigned preview_logs=0;
            if(preview_logs++<8)std::fprintf(stderr,"RW116_PREVIEW_REPLACED task=%llu list=%08X batches=%zu xr=%d\n",
                static_cast<unsigned long long>(task.sequence),preview_replacement.physical_address,
                preview_replacement.batches.size(),xr_enabled()?1:0);
        }
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr,
                "RW022_TASK_SUMMARY task=%llu task_guest=0x%08X dl_guest=0x%08X "
                "commands=%zu triangles=%u rectangles=%u scissor=%s(%d,%d,%d,%d) "
                "color_image=%s0x%08X/%u format=%u size=%u framebuffer_rgb_changed=%s "
                "changed_pixels=%zu non_black=%zu first_draw=%s blocker=%s\n",
                static_cast<unsigned long long>(task.sequence), context.rsp_task_address,
                task.task_data_address,
                raw_stats_.command_count, raw_stats_.triangle_count,
                raw_stats_.rectangle_count, raw_stats_.scissor_set ? "" : "na:",
                raw_stats_.scissor_x, raw_stats_.scissor_y,
                raw_stats_.scissor_width, raw_stats_.scissor_height,
                raw_stats_.color_image_set ? "" : "na:",
                raw_stats_.color_image_address, raw_stats_.color_image_width,
                raw_stats_.color_image_format, raw_stats_.color_image_size,
                (::xr64::render_diagnostics::readbacks() ? (framebuffer_changed_ ? "yes" : "no") : "not-sampled"), framebuffer_changed_pixels_,
                framebuffer_non_black_pixels_, first_draw_operation_.empty() ? "none" : first_draw_operation_.c_str(),
                translated ? "none" : error.c_str());
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        return translated;
    }

    bool begin_frame(std::uint32_t width, std::uint32_t height,
            std::string &) override {
#ifdef XR64_OPENXR
        g_xr_retained_geometry.restore_draw_scope();
#endif
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_OPENGL_FRAME_BEGIN width=%u height=%u\n",
                width, height);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        logical_width_ = static_cast<int>(width);
        logical_height_ = static_cast<int>(height);
        guest_viewport_set_ = false;
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_TEXTURE_2D);
        set_drawable_viewport();
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        // glClear respects the depth-write mask left by the last HUD/rectangle.
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        framebuffer_changed_ = false;
        framebuffer_changed_pixels_ = 0;
        framebuffer_non_black_pixels_ = 0;
        first_draw_operation_.clear();
        capture_framebuffer("RW021_FB_FRAME_BEGIN_AFTER_CLEAR");
        frame_start_framebuffer_ = last_framebuffer_;
        frame_start_width_ = last_framebuffer_width_;
        frame_start_height_ = last_framebuffer_height_;
        return true;
    }

    bool set_viewport(int x, int y, int width, int height, std::string &) override {
        // The raw executor has already projected vertices in the N64 clip
        // space. Keep the host drawable as the viewport and use the N64
        // logical dimensions for the fixed-function projection below.
        const bool first_viewport = !guest_viewport_set_;
        guest_viewport_set_ = true;
        guest_viewport_x_ = x;
        guest_viewport_y_ = y;
        guest_viewport_width_ = std::max(1, width);
        guest_viewport_height_ = std::max(1, height);
        // An inset viewport does not resize the color image. Preserve the
        // established frame extent for pixel projection, scissor and clipping.
        const int right = std::max(1, x + guest_viewport_width_);
        const int bottom = std::max(1, y + guest_viewport_height_);
        if (!color_image_set_) logical_width_ = first_viewport ? right : std::max(logical_width_, right);
        logical_height_ = first_viewport ? bottom : std::max(logical_height_, bottom);
        set_drawable_viewport();
        apply_guest_scissor();
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr,
                "RW043_GUEST_VIEWPORT task=%llu guest=(%d,%d,%d,%d) "
                "logical=(%d,%d)\n",
                static_cast<unsigned long long>(task_sequence_), x, y, width,
                height, logical_width_, logical_height_);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        return true;
    }

    bool set_scissor(int x, int y, int width, int height,
            std::string &) override {
        guest_scissor_set_ = true;
        guest_scissor_x_ = x;
        guest_scissor_y_ = y;
        guest_scissor_width_ = width;
        guest_scissor_height_ = height;
        apply_guest_scissor();
        return true;
    }

    bool set_color_image(std::uint32_t address, std::uint32_t width,
            std::string &) override {
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_RDP_COLOR_IMAGE address=0x%08X width=%u\n",
                address, width);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        color_image_address_ = address & 0x1FFFFFFFU;
        color_image_set_ = true;
        logical_width_ = static_cast<int>(std::max(1U, width));
        apply_guest_scissor();
        return true;
    }

    bool set_depth_image(std::uint32_t address, std::string &) override {
        depth_image_address_ = address & 0x1FFFFFFFU;
        depth_image_known_ = true;
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_RDP_DEPTH_IMAGE\n");
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        return true;
    }

    bool upload_texture(const ::xr64::N64RawFast3DTexture &texture,
            std::string &) override {
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_RDP_TEXTURE_UPLOAD width=%u height=%u "
                "format=%u size=%u\n", texture.width, texture.height,
                texture.format, texture.size);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        if (transient_texture_id_) {
            glDeleteTextures(1, &transient_texture_id_);
            transient_texture_id_ = 0;
        }
        const auto upload_begin = std::chrono::steady_clock::now();
        const auto resource = g_texture_cache.acquire(texture, immutable_payload_);
        texture_id_ = resource.id;
        if (!resource.retained) transient_texture_id_ = resource.id;
#ifdef XR64_OPENXR
        if (g_xr_eye && resource.uploaded) {
            g_upload_cpu_ms += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-upload_begin).count();
            ++g_upload_calls;
        }
#endif
        if (diagnostic_task_selected()) alpha_probe_cpu_ = texture.rgba;
        texture_alpha_precombined_ = texture.alpha_precombined;
        texture_format_ = texture.format; texture_size_ = texture.size;
        texture_width_ = static_cast<int>(texture.width);
        texture_height_ = static_cast<int>(texture.height);
        if (resource.uploaded) ++texture_upload_count_;
        if (diagnostic_task_selected() && ::xr64::render_diagnostics::texture_capture()) {
            GLint unpack_alignment = 0, unpack_row_length = 0, unpack_skip_rows = 0, unpack_skip_pixels = 0;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack_skip_rows);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack_skip_pixels);
            GLint pack_alignment = 0, pack_row_length = 0, pack_skip_rows = 0, pack_skip_pixels = 0;
            glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
            glGetIntegerv(GL_PACK_ROW_LENGTH, &pack_row_length);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &pack_skip_rows);
            glGetIntegerv(GL_PACK_SKIP_PIXELS, &pack_skip_pixels);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            std::vector<std::uint8_t> uploaded(texture.rgba.size());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, uploaded.data());
            glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
            glPixelStorei(GL_PACK_ROW_LENGTH, pack_row_length);
            glPixelStorei(GL_PACK_SKIP_ROWS, pack_skip_rows);
            glPixelStorei(GL_PACK_SKIP_PIXELS, pack_skip_pixels);
            std::fprintf(stderr, "RW080_GL_TEXTURE task=%llu upload=%u dimensions=%ux%u unpack_alignment=%d row_length=%d skip_rows=%d skip_pixels=%d cpu_equals_gl=%u\n",
                    static_cast<unsigned long long>(task_sequence_), texture_upload_count_,
                    texture.width, texture.height, unpack_alignment, unpack_row_length,
                    unpack_skip_rows, unpack_skip_pixels, uploaded == texture.rgba ? 1U : 0U);
            if (const char *directory = std::getenv("XR64_RW_CAPTURE_DIR")) {
                const auto path = std::filesystem::path(directory) / ("tmem-" +
                        std::to_string(task_sequence_) + "-" + std::to_string(texture_upload_count_) + "-gl-rgba.bin");
                std::ofstream out(path, std::ios::binary);
                out.write(reinterpret_cast<const char *>(uploaded.data()), uploaded.size());
            }
        }

        if (diagnostic_task_selected() && ::xr64::render_diagnostics::texture_capture() && texture_upload_count_ == 1U) {
            const char *capture_env = std::getenv("XR64_RW_CAPTURE_DIR");
            if (capture_env != nullptr && capture_env[0] != '\0') {
                const std::filesystem::path path = std::filesystem::path(capture_env) /
                        ("task-" + std::to_string(task_sequence_) + "-decoded-texture.bmp");
                const bool saved = write_bmp(path, texture_width_, texture_height_, texture.rgba);
                std::fprintf(stderr,
                        "RW046_DECODED_TEXTURE task=%llu upload=1 path=%s saved=%u\n",
                        static_cast<unsigned long long>(task_sequence_),
                        path.string().c_str(), saved ? 1U : 0U);
                XR64_RENDER_DIAGNOSTIC_FLUSH();
            }
        }
        return true;
    }

    bool draw_triangles(
            const std::vector<::xr64::N64RawFast3DVertex> &input_vertices,
            const ::xr64::N64RawFast3DDrawState &state,
            std::string &error) override {
        return draw_triangles_view(input_vertices.data(), input_vertices.size(),
                state, error);
    }

    bool draw_triangles_view(
            const ::xr64::N64RawFast3DVertex *input_vertices,
            std::size_t vertex_count,
            const ::xr64::N64RawFast3DDrawState &state,
            std::string &) override {
#ifdef XR64_OPENXR
        const bool retarget_xr=g_xr_eye && local_scene_projection(state,local_camera_);
        const XrEyeFrame* raw_eye=g_xr_eye;
        XrEyeFrame world_eye;
        if(retarget_xr){world_eye=local_xr_eye(*raw_eye,local_camera_,xr_world_units());g_xr_eye=&world_eye;}
        const bool retained=!state.tracked_attachment && draw_xr_retained_triangles(input_vertices,vertex_count,state);
        g_xr_eye=raw_eye;
        if(retained)return true;
        g_xr_retained_geometry.restore_draw_scope();
#endif
        int drawable_w=0,drawable_h=0;
        g_sdl.gl_get_drawable_size(window_,&drawable_w,&drawable_h);
        const bool scene_viewport =
                ::xr64::n64_raw_fast3d_widescreen_viewport_eligible(
                        state.viewport, logical_width_, logical_height_);
        const auto adjustment = desktop_projection_adjustment(
                menu_panel_, xr_enabled(), state.perspective_projection, scene_viewport,
                g_widescreen, drawable_w, drawable_h, g_diagnostic_fov,
                state.perspective_y_scale);
        const bool wide = adjustment.wide;
        std::vector<::xr64::N64RawFast3DVertex> adjusted_vertices;
        if (vertex_count) adjusted_vertices.assign(input_vertices,
                input_vertices + vertex_count);
        const bool retarget=!menu_panel_ && local_scene_projection(state,local_camera_);
        for (auto& v : adjusted_vertices) {
            if(retarget)v=local_project_vertex(v,state,local_camera_);
            v.x *= adjustment.x_scale;
            v.y *= adjustment.y_scale;
        }
#ifdef XR64_OPENXR
        if(g_xr_eye && !g_xr_menu_flat && state.perspective_projection && state.perspective_x_norm>0.00001F && state.perspective_y_norm>0.00001F && state.perspective_w_norm>0.00001F) {
            // Recover eye-space coordinates from the symmetric guest projection.
            // Norms remove camera rotation and N64 perspective normalization.
            // World scale is deliberately explicit pending headset calibration.
            const auto& eye=*g_xr_eye;
            float units=100.0F;
            if(const char* value=std::getenv("XR64_XR_UNITS_PER_METRE")) { const float parsed=std::strtof(value,nullptr);if(std::isfinite(parsed)&&parsed>0)units=parsed; }
            if (vertex_count) adjusted_vertices.assign(input_vertices,
                    input_vertices + vertex_count);
            if(state.tracked_attachment) {
                const auto attachment=tracked_weapon_attachment(presentation_motion_);
                if(!attachment.valid)return true;
                for(auto& v:adjusted_vertices) {
                    const auto p=attachment.point({v.x,v.y,v.z});
                    const auto clip=xr_project_tracked_point(p,eye);
                    v.x=clip.x;v.y=clip.y;v.z=clip.z;v.w=clip.w;
                }
            } else for(auto& v:adjusted_vertices)v=xr_project_vertex(v,state,retarget_xr?world_eye:eye,units);
        }
#endif
        set_drawable_viewport(wide);
        apply_guest_scissor(wide);
        const auto vertices = ::xr64::n64_clip_triangles(adjusted_vertices, state.viewport,
                static_cast<float>(logical_width_), static_cast<float>(logical_height_));
        if (diagnostic_task_selected()) std::fprintf(stderr,
                "RW087_CLIP task=%llu input=%zu output=%zu\n",
                static_cast<unsigned long long>(task_sequence_),vertex_count,vertices.size());
        if (vertices.empty()) return true;

        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_RDP_TRIANGLE_SUBMIT vertices=%zu\n",
                vertices.size());
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        set_triangle_draw_state(state);
        if (diagnostic_task_selected()) {
            GLint min_filter=0, mag_filter=0;
            if (state.textured && texture_id_) {
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag_filter);
            }
            std::fprintf(stderr, "RW085_SHADING task=%llu bypass=%u min_filter=%d mag_filter=%d gl_lighting=%u texture_only_rgb=%u\n",
                    static_cast<unsigned long long>(task_sequence_), g_bypass_mesh_shading?1U:0U,
                    min_filter, mag_filter, glIsEnabled(GL_LIGHTING)?1U:0U,
                    ::xr64::n64_raw_fast3d_texture_rgb_material(state)?1U:0U);
        }
        trace_mesh(vertices, state);
        const bool diagnostic_triangle_path = ::xr64::render_diagnostics::legacy() ||
                diagnostic_task_selected();
        const char *fallback_reason = nullptr;
        if (diagnostic_triangle_path) fallback_reason = "diagnostic_mode";
#ifdef XR64_OPENXR
        else if (g_xr_eye && !xr_triangle_vbo_enabled())
            fallback_reason = "xr_vbo_disabled";
#endif
        else if (!g_triangle_vbo.ensure()) fallback_reason = g_triangle_vbo.reason;
        else if (vertices.size() > kTriangleVboByteLimit / sizeof(TriangleArrayVertex))
            fallback_reason = "batch_exceeds_32_mib_limit";
        if (fallback_reason) {
            note_triangle_submission(false, fallback_reason);
        } else {
            note_triangle_submission(true);
            draw_triangles_vbo(vertices, state);
        }
        if (fallback_reason) {
            glBegin(GL_TRIANGLES);
            for (const auto &vertex : vertices) {
                if (state.textured) {
                    if (state.other_mode & (1ULL << 51U)) {
                        // Preserve RDP perspective texture interpolation after CPU clipping/divide.
                        const float q=1.0F/vertex.w;
                        glTexCoord4f(vertex.s*q,vertex.t*q,0.0F,q);
                    } else glTexCoord2f(vertex.s, vertex.t);
                }
                // Diagnostic RGB-only bypass: preserve material alpha and visibility state.
                if (::xr64::n64_raw_fast3d_palette_lerp_material(state)) {
                    const auto &c = state.environment_color;
                    glColor4ub(c.r, c.g, c.b, c.a);
                } else {
                glColor4ub(g_bypass_mesh_shading ? 255 : vertex.color.r,
                        g_bypass_mesh_shading ? 255 : vertex.color.g,
                        g_bypass_mesh_shading ? 255 : vertex.color.b, vertex.color.a);
                }
                ::xr64::N64RawFast3DGuestPosition guest;
                if (!::xr64::n64_raw_fast3d_map_to_guest_viewport(
                            vertex, state.viewport, guest)) {
                    std::fprintf(stderr,
                            "RW078_TRIANGLE_REJECT task=%llu reason=zero_w clip=(%.6f,%.6f,%.6f,%.6f)\n",
                            static_cast<unsigned long long>(task_sequence_),
                            vertex.x, vertex.y, vertex.z, vertex.w);
                    XR64_RENDER_DIAGNOSTIC_FLUSH();
                    continue;
                }
                if (diagnostic_task_selected()) {
                    std::fprintf(stderr,
                            "RW078_TRIANGLE_VERTEX task=%llu clip=(%.6f,%.6f,%.6f,%.6f) "
                            "ndc=(%.6f,%.6f,%.6f) viewport_scale=(%d,%d,%d) "
                            "viewport_translate=(%d,%d,%d) guest=(%.6f,%.6f,%.6f) "
                            "logical=(%d,%d) attributes=(%u,%u,%u,%u) vertex_geom=0x%08X draw_geom=0x%08X\n",
                            static_cast<unsigned long long>(task_sequence_),
                            vertex.x, vertex.y, vertex.z, vertex.w,
                            guest.ndc_x, guest.ndc_y, guest.ndc_z,
                            state.viewport.scale_x, state.viewport.scale_y, state.viewport.scale_z,
                            state.viewport.translate_x, state.viewport.translate_y, state.viewport.translate_z,
                            guest.x, guest.y, guest.z, logical_width_, logical_height_,
                            static_cast<unsigned>(vertex.color.r), static_cast<unsigned>(vertex.color.g),
                            static_cast<unsigned>(vertex.color.b), static_cast<unsigned>(vertex.color.a),
                            vertex.geometry_mode_at_load, state.geometry_mode);
                    XR64_RENDER_DIAGNOSTIC_FLUSH();
                }
                glVertex3f(guest.x, guest.y,
                        ::xr64::n64_raw_fast3d_pixel_projection_z(guest.ndc_z));
            }
            glEnd();
        }
        observe_framebuffer_change("triangle");
        return true;
    }

    bool draw_rectangle(float left, float top, float right, float bottom,
            float s0, float t0, float s1, float t1,
            const ::xr64::N64RawFast3DDrawState &state,
            std::string &) override {
#ifdef XR64_OPENXR
        g_xr_retained_geometry.restore_draw_scope();
#endif
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_RDP_RECT_SUBMIT left=%.2f top=%.2f "
                "right=%.2f bottom=%.2f textured=%u\n", left, top, right,
                bottom, state.textured ? 1U : 0U);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        // RDP clears Z by temporarily selecting the depth image as color target.
        // It must never paint the host color buffer.
        if (depth_image_known_ && color_image_address_ == depth_image_address_ &&
                !state.textured && ((state.other_mode >> 52U) & 3U) == 3U &&
                state.fill_color.r==255 && state.fill_color.g==255 &&
                state.fill_color.b==247 && state.fill_color.a==0) {
            glPushAttrib(GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT);
            int dw=0,dh=0;g_sdl.gl_get_drawable_size(window_, &dw, &dh);
            #ifdef XR64_OPENXR
            const bool flat_menu = g_xr_menu_flat && g_xr_eye;
            if (flat_menu) { dw = g_xr_eye->width; dh = g_xr_eye->height; }
#else
            const bool flat_menu = false;
#endif
            const auto area=(flat_menu || g_widescreen) ?
                    ::xr64::render_diagnostics::Viewport{0,0,dw,dh} :
                    ::xr64::render_diagnostics::fit_4_3(dw,dh);
            const float scale_x=float(area.width)/logical_width_, scale_y=float(area.height)/logical_height_;
            glEnable(GL_SCISSOR_TEST);
            glScissor(area.x+int(left*scale_x),area.y+int((logical_height_-bottom)*scale_y),
                    int((right-left)*scale_x),int((bottom-top)*scale_y));
            glDepthMask(GL_TRUE);glClearDepth(1.0);glClear(GL_DEPTH_BUFFER_BIT);glPopAttrib();
            if(diagnostic_task_selected())std::fprintf(stderr,"RW088_DEPTH_TARGET_CLEAR task=%llu address=0x%08X\n",
                    static_cast<unsigned long long>(task_sequence_),depth_image_address_);
            return true;
        }
        set_drawable_viewport();
        apply_guest_scissor();
        set_blend_depth(state);
        set_pixel_projection();
        if (state.textured && texture_id_ != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture_id_);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        if (state.textured && texture_id_ != 0) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glDisable(GL_CULL_FACE);
        auto color = !state.textured ?
                startup_rectangle_fill(menu_panel_, startup_, left, top, right, bottom,
                        logical_width_, logical_height_, state) :
                texture_alpha_precombined_ &&
                        ::xr64::n64_raw_fast3d_environment_hud_material(state) ?
                state.environment_color : state.primitive_color;
        glColor4ub(color.r, color.g, color.b,
                state.textured && texture_alpha_precombined_ ? 255 : color.a);
        // Captured health digits only; this probe does not alter draw state.
        if (diagnostic_task_selected() && state.textured && left < logical_width_*0.25F &&
                top > logical_height_*0.8F && !alpha_probe_cpu_.empty()) {
            glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
            glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ROW_LENGTH,0);
            glPixelStorei(GL_PACK_SKIP_ROWS,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0);
            std::vector<std::uint8_t> uploaded(alpha_probe_cpu_.size());
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,uploaded.data());
            glPopClientAttrib();
            unsigned zero=0, partial=0, opaque=0;
            for (std::size_t i=3;i<alpha_probe_cpu_.size();i+=4) {
                const auto a=alpha_probe_cpu_[i];
                if(a==0)++zero; else if(a==255)++opaque; else ++partial;
            }
            GLint unpack=0,row=0,env=0,src=0,dst=0;
            glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
            glGetTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,&env);
            glGetIntegerv(GL_BLEND_SRC,&src);glGetIntegerv(GL_BLEND_DST,&dst);
            std::fprintf(stderr,"RW093_HEALTH_ALPHA task=%llu upload=%u rect=(%.1f,%.1f,%.1f,%.1f) uv=(%.6f,%.6f,%.6f,%.6f) size=%dx%d format=%u/%u alpha_zero=%u partial=%u opaque=%u cpu_equals_gl=%u unpack=%d row=%d other=0x%016llX combine=%08X/%08X primitive_alpha=%u texenv=%d blend=%u src=%d dst=%d alpha_test=%u\n",
                static_cast<unsigned long long>(task_sequence_),texture_upload_count_,left,top,right,bottom,
                s0,t0,s1,t1,texture_width_,texture_height_,texture_format_,texture_size_,zero,partial,opaque,
                uploaded==alpha_probe_cpu_,unpack,row,static_cast<unsigned long long>(state.other_mode),
                state.combine_word0,state.combine_word1,state.primitive_color.a,env,glIsEnabled(GL_BLEND),src,dst,glIsEnabled(GL_ALPHA_TEST));
            if(const char* dir=std::getenv("XR64_RW_CAPTURE_DIR")) {
                const auto base=std::filesystem::path(dir)/("task-"+std::to_string(task_sequence_)+"-health-"+std::to_string(texture_upload_count_));
                std::ofstream cpu(base.string()+"-cpu.rgba",std::ios::binary);
                cpu.write(reinterpret_cast<const char*>(alpha_probe_cpu_.data()),alpha_probe_cpu_.size());
                std::ofstream gpu(base.string()+"-gl.rgba",std::ios::binary);
                gpu.write(reinterpret_cast<const char*>(uploaded.data()),uploaded.size());
            }
        }
        if (!state.textured) {
            ++fill_rectangle_count_;
            if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr,
                    "RW021_F6_STATE index=%zu fill_rgba=(%u,%u,%u,%u) "
                    "primitive_rgba=(%u,%u,%u,%u) other_mode=0x%016llX "
                    "alpha_blend=%u depth_test=%u depth_write=%u depth_compare=%u\n",
                    fill_rectangle_count_, static_cast<unsigned>(state.fill_color.r),
                    static_cast<unsigned>(state.fill_color.g),
                    static_cast<unsigned>(state.fill_color.b),
                    static_cast<unsigned>(state.fill_color.a),
                    static_cast<unsigned>(state.primitive_color.r),
                    static_cast<unsigned>(state.primitive_color.g),
                    static_cast<unsigned>(state.primitive_color.b),
                    static_cast<unsigned>(state.primitive_color.a),
                    static_cast<unsigned long long>(state.other_mode),
                    state.alpha_blend ? 1U : 0U, state.depth_test ? 1U : 0U,
                    state.depth_write ? 1U : 0U, state.depth_compare ? 1U : 0U);
            XR64_RENDER_DIAGNOSTIC_FLUSH();
            capture_framebuffer("RW021_FB_BEFORE_F6");
        }
        glBegin(GL_QUADS);
        glTexCoord2f(s0, t0); glVertex3f(left, top, 0.0F);
        glTexCoord2f(s1, t0); glVertex3f(right, top, 0.0F);
        glTexCoord2f(s1, t1); glVertex3f(right, bottom, 0.0F);
        glTexCoord2f(s0, t1); glVertex3f(left, bottom, 0.0F);
        glEnd();
        observe_framebuffer_change("rectangle");
        if (state.textured) {
            ++textured_rectangle_count_;
            if (diagnostic_task_selected() && textured_rectangle_count_ == 1U) {
                const char *capture_env = std::getenv("XR64_RW_CAPTURE_DIR");
                std::filesystem::path path;
                if (capture_env != nullptr && capture_env[0] != '\0') {
                    path = std::filesystem::path(capture_env) /
                            ("task-" + std::to_string(task_sequence_) + "-draw-1.bmp");
                }
                std::fprintf(stderr,
                        "RW046_DRAW task=%llu rectangle=1 coords=(%.2f,%.2f)-(%.2f,%.2f) "
                        "texcoords=(%.6f,%.6f)-(%.6f,%.6f) texture=%dx%d "
                        "other_mode=0x%016llX combine=(0x%08X,0x%08X) "
                        "primitive=(%u,%u,%u,%u)\n",
                        static_cast<unsigned long long>(task_sequence_), left, top, right, bottom,
                        s0, t0, s1, t1, texture_width_, texture_height_,
                        static_cast<unsigned long long>(state.other_mode),
                        state.combine_word0, state.combine_word1,
                        static_cast<unsigned>(state.primitive_color.r),
                        static_cast<unsigned>(state.primitive_color.g),
                        static_cast<unsigned>(state.primitive_color.b),
                        static_cast<unsigned>(state.primitive_color.a));
                XR64_RENDER_DIAGNOSTIC_FLUSH();
                capture_framebuffer("RW046_DRAW_1", path.empty() ? nullptr : &path);
            }
        }
        if (!state.textured) capture_framebuffer("RW021_FB_AFTER_F6");
        return true;
    }

    bool clear_depth(std::string &) override {
#ifdef XR64_OPENXR
        g_xr_retained_geometry.restore_draw_scope();
#endif
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_RDP_CLEAR_DEPTH\n");
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        glClear(GL_DEPTH_BUFFER_BIT);
        return true;
    }

    bool end_frame(std::string &) override {
#ifdef XR64_OPENXR
        g_xr_retained_geometry.restore_draw_scope();
#endif
        // Decoded task EndFrame marks command order; the presentation owner
        // performs the single swap after replay (or after both XR eyes).
        if (defer_presentation_) return true;
#ifdef XR64_OPENXR
        if(g_xr_eye) return true; // The OpenXR frame owner releases and presents both eyes.
#endif
        int window_width = 0;
        int window_height = 0;
        int drawable_width = 0;
        int drawable_height = 0;
        g_sdl.get_window_size(window_, &window_width, &window_height);
        g_sdl.gl_get_drawable_size(window_, &drawable_width, &drawable_height);
        GLint viewport[4] = {};
        GLint scissor[4] = {};
        GLint framebuffer = -1;
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissor);
        glGetIntegerv(0x8CA6, &framebuffer);
        const auto *vi = ultramodern::renderer::get_vi_regs();
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr,
                "RW043_PRESENTATION task=%llu color_image=%s0x%08X width=%u "
                "format=%u size=%u vi_origin=0x%08X vi_width=%u vi_status=0x%08X "
                "vi_h_start=0x%08X vi_v_start=0x%08X vi_x_scale=0x%08X "
                "vi_y_scale=0x%08X gl_framebuffer=%d gl_target=(%d,%d) "
                "viewport=(%d,%d,%d,%d) scissor=(%d,%d,%d,%d) "
                "scissor_enabled=%u sdl_window=(%d,%d) drawable=(%d,%d) "
                "presentation=SDL_GL_SwapWindow source=(0,0,%d,%d) "
                "destination=(0,0,%d,%d) framebuffer_blit=none "
                "intermediate_gl_target=%s\n",
                static_cast<unsigned long long>(task_sequence_),
                raw_stats_.color_image_set ? "" : "na:",
                raw_stats_.color_image_address, raw_stats_.color_image_width,
                raw_stats_.color_image_format, raw_stats_.color_image_size,
                vi != nullptr ? vi->VI_ORIGIN_REG : 0U,
                vi != nullptr ? vi->VI_WIDTH_REG : 0U,
                vi != nullptr ? vi->VI_STATUS_REG : 0U,
                vi != nullptr ? vi->VI_H_START_REG : 0U,
                vi != nullptr ? vi->VI_V_START_REG : 0U,
                vi != nullptr ? vi->VI_X_SCALE_REG : 0U,
                vi != nullptr ? vi->VI_Y_SCALE_REG : 0U,
                framebuffer, drawable_width, drawable_height,
                viewport[0], viewport[1], viewport[2], viewport[3],
                scissor[0], scissor[1], scissor[2], scissor[3],
                glIsEnabled(GL_SCISSOR_TEST) ? 1U : 0U,
                window_width, window_height, drawable_width, drawable_height,
                drawable_width, drawable_height, window_width, window_height,
                framebuffer == 0 ? "none-default-framebuffer" : "bound-fbo");
        XR64_RENDER_DIAGNOSTIC_FLUSH();

#ifdef XR64_DEMO_BUILD
#if defined(XR64_FAULT_FRAME_CAPTURE)
        // Private diagnostic build: one ordinary frame per marker; no RAM/input/state injection.
        static std::uint64_t fault_capture_sequence = 0;
        std::filesystem::path fault_capture_directory;
        bool fault_capture_requested = false;
        if (const char* directory = std::getenv("XR64_FAULT_CAPTURE_DIR")) {
            if (directory[0] && fault_capture_sequence < 64U) {
                fault_capture_directory = directory;
                std::error_code error;
                fault_capture_requested = std::filesystem::remove(
                    fault_capture_directory / "capture-next-presented", error);
                if (fault_capture_requested) ++fault_capture_sequence;
            }
        }
        if (fault_capture_requested) capture_fault_frame(fault_capture_directory,
            fault_capture_sequence, "back");
#endif
        glFlush();
        g_sdl.gl_swap_window(window_);
#if defined(XR64_FAULT_FRAME_CAPTURE)
        if (fault_capture_requested) {
            GLint previous_read_buffer = GL_BACK;
            glGetIntegerv(GL_READ_BUFFER, &previous_read_buffer);
            glReadBuffer(GL_FRONT);
            capture_fault_frame(fault_capture_directory, fault_capture_sequence, "front");
            glReadBuffer(static_cast<GLenum>(previous_read_buffer));
        }
#endif
#else
        std::filesystem::path capture_dir;
        const char *capture_env = std::getenv("XR64_RW_CAPTURE_DIR");
        bool capture_task_selected = false;
        const char *capture_task_env = std::getenv("XR64_RW_CAPTURE_TASK");
        if (capture_task_env != nullptr && capture_task_env[0] != '\0') {
            char *capture_task_end = nullptr;
            const unsigned long long capture_task_first = std::strtoull(
                    capture_task_env, &capture_task_end, 10);
            if (capture_task_end != capture_task_env) {
                unsigned long long capture_task_last = capture_task_first;
                if (*capture_task_end == '-') {
                    const char *last_text = capture_task_end + 1;
                    capture_task_last = std::strtoull(last_text, &capture_task_end, 10);
                    if (capture_task_end == last_text) capture_task_last = 0;
                }
                if (*capture_task_end == '\0' && capture_task_first <= capture_task_last &&
                        task_sequence_ >= capture_task_first &&
                        task_sequence_ <= capture_task_last) {
                    capture_task_selected = true;
                }
            }
        }
        bool capture_xr_frame = false;
        if (xr_enabled() && capture_env != nullptr && capture_env[0] != '\0') {
            std::error_code marker_error;
            const bool marker_capture = std::filesystem::remove(
                    std::filesystem::path(capture_env) / "capture-next-xr-frame", marker_error);
            capture_xr_frame = g_capture_next_xr_frame.exchange(false) || marker_capture;
        }
        const bool capture_requested = capture_env != nullptr && capture_env[0] != '\0' &&
                (capture_xr_frame || (!suppress_capture_ && capture_task_selected));
        if (capture_requested) {
            capture_dir = capture_env;
            std::error_code directory_error;
            std::filesystem::create_directories(capture_dir, directory_error);
            if (directory_error) {
                std::fprintf(stderr, "RW043_CAPTURE_ERROR task=%llu stage=create_directory "
                        "path=%s code=%d\n",
                        static_cast<unsigned long long>(task_sequence_),
                        capture_dir.string().c_str(), directory_error.value());
                XR64_RENDER_DIAGNOSTIC_FLUSH();
            }
        }
        const std::string stem = capture_xr_frame
                ? "xr-frame-" + std::to_string(++g_xr_capture_count) + "-task-" + std::to_string(task_sequence_)
                : "task-" + std::to_string(task_sequence_);
        if (capture_xr_frame) std::fprintf(stderr, "RW111_XR_MIRROR_CAPTURE frame=%s\n", stem.c_str());
        const std::filesystem::path pre_swap_path = capture_dir / (stem + "-pre-swap.bmp");
        capture_framebuffer("RW043_GL_PRE_SWAP",
                capture_requested ? &pre_swap_path : nullptr);
        glFlush();
        log_gl_errors("RW021_BEFORE_SWAP");
        draw_diagnostic_hud(drawable_width, drawable_height);
        if (capture_requested && g_diagnostic_hud) {
            const auto hud_path = capture_dir / (stem + "-hud.bmp");
            capture_framebuffer("RW081_HUD_CAPTURE", &hud_path);
        }
        g_sdl.gl_swap_window(window_);
        GLint prior_read_buffer = GL_BACK;
        glGetIntegerv(GL_READ_BUFFER, &prior_read_buffer);
        glReadBuffer(GL_FRONT);
        const std::filesystem::path front_path = capture_dir / (stem + "-presented-front.bmp");
        capture_framebuffer("RW043_GL_PRESENTED_FRONT",
                capture_requested ? &front_path : nullptr);
        glReadBuffer(static_cast<GLenum>(prior_read_buffer));
#endif
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW020_RENDERER_SUCCESS openGL_frame_complete "
                "sdl_swap_presented=1\n");
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        static const bool cache_metrics = [] {
            const char* option = std::getenv("XR64_RW_PERF");
            return option && option[0] == '1' && option[1] == '\0';
        }();
        if (cache_metrics) {
            static std::uint64_t presentations = 0;
            static std::uint64_t last_hits = 0, last_misses = 0, last_evictions = 0;
            if (++presentations % 300U == 0U) {
                const auto hits = g_texture_cache.hits();
                const auto misses = g_texture_cache.misses();
                const auto evictions = g_texture_cache.evictions();
                std::fprintf(stderr,
                        "RW_TEXTURE_CACHE presentations=%llu hits=%llu misses=%llu evictions=%llu resident_bytes=%zu budget_bytes=%zu\n",
                        static_cast<unsigned long long>(presentations),
                        static_cast<unsigned long long>(hits - last_hits),
                        static_cast<unsigned long long>(misses - last_misses),
                        static_cast<unsigned long long>(evictions - last_evictions),
                        g_texture_cache.bytes(), static_cast<std::size_t>(64U) * 1024U * 1024U);
                last_hits = hits; last_misses = misses; last_evictions = evictions;
            }
        }
        return true;
    }

private:
    SDL_Window *window_;
    std::uint64_t task_sequence_ = 0;
    bool immutable_payload_ = false;
    bool defer_presentation_ = false;
    const ::xr64::N64FrameSnapshotOf<LocalPlayerPresentation>* frame_snapshot_ = nullptr;
    LocalPlayerPresentation scene_player_{};
    LocalCameraCorrection local_camera_{};
    XrMotionInput presentation_motion_{};
    bool menu_panel_ = true;
    bool startup_ = false;
    GLuint texture_id_ = 0;
    GLuint transient_texture_id_ = 0;
    std::chrono::steady_clock::time_point frame_begin_{};
    bool suppress_capture_ = false;
    std::ofstream mesh_trace_;
    unsigned mesh_draw_count_ = 0;
    std::uint32_t texture_format_ = 0, texture_size_ = 0;
    std::vector<std::uint8_t> alpha_probe_cpu_;
    bool texture_alpha_precombined_ = false;
    int texture_width_ = 1;
    int texture_height_ = 1;
    int logical_width_ = 320;
    int logical_height_ = 240;
    bool color_image_set_ = false;
    std::uint32_t color_image_address_ = 0, depth_image_address_ = 0;
    bool depth_image_known_ = false;
    bool guest_viewport_set_ = false;
    int guest_viewport_x_ = 0;
    int guest_viewport_y_ = 0;
    int guest_viewport_width_ = 320;
    int guest_viewport_height_ = 240;
    bool guest_scissor_set_ = false;
    int guest_scissor_x_ = 0;
    int guest_scissor_y_ = 0;
    int guest_scissor_width_ = 0;
    int guest_scissor_height_ = 0;
    std::size_t fill_rectangle_count_ = 0;
    std::size_t texture_upload_count_ = 0;
    std::size_t textured_rectangle_count_ = 0;
    ::xr64::N64RawFast3DTaskStats raw_stats_{};
    bool framebuffer_changed_ = false;
    std::size_t framebuffer_changed_pixels_ = 0;
    std::size_t framebuffer_non_black_pixels_ = 0;
    std::string first_draw_operation_;
    std::vector<std::uint8_t> last_framebuffer_;
    int last_framebuffer_width_ = 0;
    int last_framebuffer_height_ = 0;
    std::vector<std::uint8_t> frame_start_framebuffer_;
    int frame_start_width_ = 0;
    int frame_start_height_ = 0;

    static void log_gl_errors(const char *stage) {
#if !XR64_RENDER_DIAGNOSTICS
        (void)stage;
        return;
#else
        GLenum error = GL_NO_ERROR;
        bool reported = false;
        while ((error = glGetError()) != GL_NO_ERROR) {
            std::fprintf(stderr, "RW021_GL_ERROR stage=%s code=0x%04X\n",
                    stage, static_cast<unsigned>(error));
            ++g_gl_error_count;
            reported = true;
        }
        if (reported) XR64_RENDER_DIAGNOSTIC_FLUSH();
#endif
    }

    void set_drawable_viewport(bool wide = false) const {
#ifdef XR64_OPENXR
        if(g_xr_eye) {glViewport(0,0,g_xr_eye->width,g_xr_eye->height);return;}
#endif
        int width = 0;
        int height = 0;
        g_sdl.gl_get_drawable_size(window_, &width, &height);
        const auto area = wide ? ::xr64::render_diagnostics::Viewport{0,0,width,height} : ::xr64::render_diagnostics::fit_4_3(width, height);
        glViewport(area.x, area.y, area.width, area.height);
    }

    void apply_guest_scissor(bool wide = false) const {
#ifdef XR64_OPENXR
        if(g_xr_eye) {
            if(!guest_scissor_set_) {glDisable(GL_SCISSOR_TEST);return;}
            glEnable(GL_SCISSOR_TEST);
            glScissor(guest_scissor_x_*g_xr_eye->width/std::max(1,logical_width_),
                (logical_height_-guest_scissor_y_-guest_scissor_height_)*g_xr_eye->height/std::max(1,logical_height_),
                std::max(0,guest_scissor_width_*g_xr_eye->width/std::max(1,logical_width_)),
                std::max(0,guest_scissor_height_*g_xr_eye->height/std::max(1,logical_height_)));return;
        }
#endif
        if (!guest_scissor_set_) return;
        int drawable_width = 0;
        int drawable_height = 0;
        g_sdl.gl_get_drawable_size(window_, &drawable_width, &drawable_height);
        const auto area = wide ? ::xr64::render_diagnostics::Viewport{0,0,drawable_width,drawable_height} : ::xr64::render_diagnostics::fit_4_3(drawable_width, drawable_height);
        const int source_width = std::max(1, logical_width_);
        const int source_height = std::max(1, logical_height_);
        const int sx = area.x + guest_scissor_x_ * area.width / source_width;
        const int sy = area.y + (source_height - guest_scissor_y_ - guest_scissor_height_) *
                area.height / source_height;
        const int sw = guest_scissor_width_ * area.width / source_width;
        const int sh = guest_scissor_height_ * area.height / source_height;
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, std::max(0, sw), std::max(0, sh));
        if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr,
                "RW043_SCISSOR_MAP task=%llu guest=(%d,%d,%d,%d) "
                "logical=(%d,%d) drawable=(%d,%d) gl=(%d,%d,%d,%d) "
                "viewport_known=%u color_image_known=%u\n",
                static_cast<unsigned long long>(task_sequence_),
                guest_scissor_x_, guest_scissor_y_, guest_scissor_width_,
                guest_scissor_height_, source_width, source_height,
                drawable_width, drawable_height, sx, sy,
                std::max(0, sw), std::max(0, sh),
                guest_viewport_set_ ? 1U : 0U, color_image_set_ ? 1U : 0U);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
    }

    static bool write_bmp(const std::filesystem::path &path,
            int width, int height, const std::vector<std::uint8_t> &rgba) {
        if (width <= 0 || height <= 0 || rgba.size() !=
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
            return false;
        }
        const std::uint32_t pixel_bytes = static_cast<std::uint32_t>(rgba.size());
        const std::uint32_t file_bytes = 54U + pixel_bytes;
        std::array<std::uint8_t, 54> header{};
        const auto put_u16 = [&header](std::size_t offset, std::uint16_t value) {
            header[offset] = static_cast<std::uint8_t>(value);
            header[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
        };
        const auto put_u32 = [&header](std::size_t offset, std::uint32_t value) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                header[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
            }
        };
        header[0] = 'B';
        header[1] = 'M';
        put_u32(2, file_bytes);
        put_u32(10, 54U);
        put_u32(14, 40U);
        put_u32(18, static_cast<std::uint32_t>(width));
        put_u32(22, static_cast<std::uint32_t>(height));
        put_u16(26, 1U);
        put_u16(28, 32U);
        put_u32(34, pixel_bytes);
        std::ofstream output(path, std::ios::binary);
        if (!output) return false;
        output.write(reinterpret_cast<const char *>(header.data()), header.size());
        std::vector<std::uint8_t> bgra(rgba.size());
        for (std::size_t offset = 0; offset < rgba.size(); offset += 4U) {
            bgra[offset] = rgba[offset + 2];
            bgra[offset + 1] = rgba[offset + 1];
            bgra[offset + 2] = rgba[offset];
            bgra[offset + 3] = rgba[offset + 3];
        }
        output.write(reinterpret_cast<const char *>(bgra.data()), bgra.size());
        return output.good();
    }


#if defined(XR64_FAULT_FRAME_CAPTURE)
    void capture_fault_frame(const std::filesystem::path& directory,
            std::uint64_t sequence, const char* side) noexcept {
        try {
            int width = 0, height = 0;
            g_sdl.gl_get_drawable_size(window_, &width, &height);
            if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return;
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            const GLenum error = glGetError();
            std::error_code file_error;
            std::filesystem::create_directories(directory, file_error);
            const auto path = directory / ("fault-frame-" + std::to_string(sequence) + "-" + side + ".bmp");
            const bool saved = error == GL_NO_ERROR && !file_error && write_bmp(path, width, height, pixels);
            char line[256]{};
            const int count = std::snprintf(line, sizeof(line),
                "RW_FAULT_FRAME sequence=%llu task=%llu side=%s width=%d height=%d gl_error=0x%04X saved=%d\n",
                static_cast<unsigned long long>(sequence), static_cast<unsigned long long>(task_sequence_),
                side, width, height, static_cast<unsigned>(error), saved ? 1 : 0);
            if (count > 0) std::fwrite(line, 1, static_cast<std::size_t>(count), stderr);
        } catch (...) { /* Capture failure leaves the ordinary frame path running. */ }
    }
#endif

    void capture_framebuffer(const char *label,
            const std::filesystem::path *output_path = nullptr) {
        if (output_path == nullptr && !::xr64::render_diagnostics::readbacks()) return;
        ++g_framebuffer_reads;
        GLint viewport[4] = {};
        GLint scissor[4] = {};
        GLint framebuffer = -1;
        GLint draw_buffer = 0;
        GLint read_buffer = 0;
        GLint depth_write = 0;
        GLboolean color_write[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissor);
        // GL_FRAMEBUFFER_BINDING is available for the OpenGL 2.1/FBO path
        // used by this backend; zero is the SDL window's default target.
        glGetIntegerv(0x8CA6, &framebuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
        glGetIntegerv(GL_READ_BUFFER, &read_buffer);
        glGetIntegerv(GL_DEPTH_WRITEMASK, &depth_write);
        glGetBooleanv(GL_COLOR_WRITEMASK, color_write);
        int drawable_width = 0;
        int drawable_height = 0;
        g_sdl.gl_get_drawable_size(window_, &drawable_width, &drawable_height);
        const int width = std::max(0, drawable_width > 0 ? drawable_width : viewport[2]);
        const int height = std::max(0, drawable_height > 0 ? drawable_height : viewport[3]);
        std::vector<std::uint8_t> pixels(
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        if (width > 0 && height > 0) {
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                    pixels.data());
        }
        log_gl_errors(label);

        std::array<unsigned, 4> min_rgba = {255U, 255U, 255U, 255U};
        std::array<unsigned, 4> max_rgba = {0U, 0U, 0U, 0U};
        std::unordered_set<std::uint32_t> unique_colors;
        unique_colors.reserve(static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) / 4U + 1U);
        std::size_t non_black = 0;
        std::size_t non_zero = 0;
        std::size_t changed = 0;
        int changed_min_x = width;
        int changed_min_y = height;
        int changed_max_x = -1;
        int changed_max_y = -1;
        const bool can_compare = width == last_framebuffer_width_ &&
                height == last_framebuffer_height_ &&
                last_framebuffer_.size() == pixels.size();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t offset =
                        (static_cast<std::size_t>(y) * width + x) * 4U;
                const std::uint8_t r = pixels[offset + 0];
                const std::uint8_t g = pixels[offset + 1];
                const std::uint8_t b = pixels[offset + 2];
                const std::uint8_t a = pixels[offset + 3];
                if (r != 0 || g != 0 || b != 0) ++non_black;
                if (r != 0 || g != 0 || b != 0 || a != 0) ++non_zero;
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    min_rgba[channel] = std::min<unsigned>(min_rgba[channel], pixels[offset + channel]);
                    max_rgba[channel] = std::max<unsigned>(max_rgba[channel], pixels[offset + channel]);
                }
                unique_colors.insert((static_cast<std::uint32_t>(r) << 24U) |
                        (static_cast<std::uint32_t>(g) << 16U) |
                        (static_cast<std::uint32_t>(b) << 8U) | a);
                if (can_compare) {
                    bool different = false;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        different = different || pixels[offset + channel] !=
                                last_framebuffer_[offset + channel];
                    }
                    if (different) {
                        ++changed;
                        changed_min_x = std::min(changed_min_x, x);
                        changed_min_y = std::min(changed_min_y, y);
                        changed_max_x = std::max(changed_max_x, x);
                        changed_max_y = std::max(changed_max_y, y);
                    }
                }
            }
        }
        std::fprintf(stderr,
                "RW021_FB_READBACK label=%s width=%d height=%d "
                "framebuffer=%d draw_buffer=0x%04X read_buffer=0x%04X "
                "viewport=(%d,%d,%d,%d) scissor=(%d,%d,%d,%d) "
                "scissor_enabled=%u blend=%u depth_test=%u depth_write=%d "
                "color_mask=(%u,%u,%u,%u) non_black=%zu non_zero=%zu "
                "unique_colors=%zu min_rgba=(%u,%u,%u,%u) max_rgba=(%u,%u,%u,%u) "
                "changed=%s%zu changed_bounds=%s\n",
                label, width, height, framebuffer,
                static_cast<unsigned>(draw_buffer), static_cast<unsigned>(read_buffer),
                viewport[0], viewport[1], viewport[2], viewport[3],
                scissor[0], scissor[1], scissor[2], scissor[3],
                glIsEnabled(GL_SCISSOR_TEST) ? 1U : 0U,
                glIsEnabled(GL_BLEND) ? 1U : 0U,
                glIsEnabled(GL_DEPTH_TEST) ? 1U : 0U, depth_write,
                color_write[0] ? 1U : 0U, color_write[1] ? 1U : 0U,
                color_write[2] ? 1U : 0U, color_write[3] ? 1U : 0U,
                non_black, non_zero, unique_colors.size(),
                min_rgba[0], min_rgba[1], min_rgba[2], min_rgba[3],
                max_rgba[0], max_rgba[1], max_rgba[2], max_rgba[3],
                can_compare ? "" : "na:", changed,
                changed_max_x >= 0 ? (std::to_string(changed_min_x) + "," +
                        std::to_string(changed_min_y) + "-" +
                        std::to_string(changed_max_x) + "," +
                        std::to_string(changed_max_y)).c_str() : "none");
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        if (output_path != nullptr) {
            const bool saved = write_bmp(*output_path, width, height, pixels);
            std::fprintf(stderr, "RW043_CAPTURE task=%llu label=%s path=%s saved=%u\n",
                    static_cast<unsigned long long>(task_sequence_), label,
                    output_path->string().c_str(), saved ? 1U : 0U);
            XR64_RENDER_DIAGNOSTIC_FLUSH();
        }
        last_framebuffer_ = std::move(pixels);
        last_framebuffer_width_ = width;
        last_framebuffer_height_ = height;
    }

    void observe_framebuffer_change(const char *operation) {
        if (!::xr64::render_diagnostics::readbacks()) return;
        ++g_framebuffer_reads;
        if (framebuffer_changed_ || frame_start_framebuffer_.empty()) return;
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int width = std::max(0, viewport[2]);
        const int height = std::max(0, viewport[3]);
        if (width != frame_start_width_ || height != frame_start_height_) return;
        std::vector<std::uint8_t> pixels(
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        if (width > 0 && height > 0) {
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                    pixels.data());
        }
        log_gl_errors("RW022_DRAW_WITNESS");
        std::size_t changed = 0;
        std::size_t non_black = 0;
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4U) {
            if (pixels[offset] != 0 || pixels[offset + 1] != 0 ||
                    pixels[offset + 2] != 0) {
                ++non_black;
            }
            if (pixels[offset] != frame_start_framebuffer_[offset] ||
                    pixels[offset + 1] != frame_start_framebuffer_[offset + 1] ||
                    pixels[offset + 2] != frame_start_framebuffer_[offset + 2]) {
                ++changed;
            }
        }
        if (changed == 0) return;
        framebuffer_changed_ = true;
        framebuffer_changed_pixels_ = changed;
        framebuffer_non_black_pixels_ = non_black;
        first_draw_operation_ = operation;
        std::fprintf(stderr,
                "RW022_FRAMEBUFFER_CHANGED operation=%s changed_pixels=%zu "
                "non_black=%zu\n", operation, changed, non_black);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
    }

    void trace_mesh(const std::vector<::xr64::N64RawFast3DVertex> &vertices,
            const ::xr64::N64RawFast3DDrawState &state) {
        ++mesh_draw_count_;
        if (!diagnostic_task_selected()) return;
        if (!mesh_trace_.is_open()) {
            const char *directory = std::getenv("XR64_RW_CAPTURE_DIR");
            if (!directory) return;
            std::filesystem::create_directories(directory);
            mesh_trace_.open(std::filesystem::path(directory) /
                    ("task-" + std::to_string(task_sequence_) + "-mesh.csv"));
            mesh_trace_ << "draw,vertex,upload,tex_width,tex_height,tex_format,tex_size,geometry,other_mode,combine0,combine1,gl_texenv,gl_cull,gl_front_face,gl_cull_face,gl_depth_test,gl_depth_func,gl_depth_write,gl_alpha_test,gl_blend,clip_x,clip_y,clip_z,clip_w,guest_x,guest_y,ndc_z,s,t,attr_r,attr_g,attr_b,attr_a,shade_r,shade_g,shade_b,shade_a,clip_planes,guest_signed_area,primitive_rgba,environment_rgba\n";
        }
        GLint texenv=0, front=0, cull=0, depthfunc=0, depthwrite=0;
        glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &texenv);
        glGetIntegerv(GL_FRONT_FACE, &front); glGetIntegerv(GL_CULL_FACE_MODE, &cull);
        glGetIntegerv(GL_DEPTH_FUNC, &depthfunc); glGetIntegerv(GL_DEPTH_WRITEMASK, &depthwrite);
        std::array<::xr64::N64RawFast3DGuestPosition,3> points{};
        bool projectable = vertices.size()==3;
        for (std::size_t i=0;i<std::min<std::size_t>(3,vertices.size());++i)
            projectable &= ::xr64::n64_raw_fast3d_map_to_guest_viewport(vertices[i],state.viewport,points[i]);
        const float area = projectable ? (points[1].x-points[0].x)*(points[2].y-points[0].y) -
                (points[1].y-points[0].y)*(points[2].x-points[0].x) : 0;
        const auto rgba = [](const ::xr64::N64RawFast3DColor &c) {
            return (std::uint32_t(c.r)<<24)|(std::uint32_t(c.g)<<16)|(std::uint32_t(c.b)<<8)|c.a;
        };
        for (std::size_t i=0;i<vertices.size();++i) {
            const auto &v=vertices[i];
            ::xr64::N64RawFast3DGuestPosition point{};
            ::xr64::n64_raw_fast3d_map_to_guest_viewport(v,state.viewport,point);
            const unsigned planes = (v.x < -v.w ? 1U:0U)|(v.x > v.w ? 2U:0U)|
                (v.y < -v.w ? 4U:0U)|(v.y > v.w ? 8U:0U)|(v.z < -v.w ? 16U:0U)|
                (v.z > v.w ? 32U:0U)|(v.w <= 0 ? 64U:0U);
            mesh_trace_ << mesh_draw_count_ << ',' << i << ',' << texture_upload_count_ << ','
                << texture_width_ << ',' << texture_height_ << ',' << texture_format_ << ',' << texture_size_ << ','
                << state.geometry_mode << ',' << state.other_mode << ',' << state.combine_word0 << ',' << state.combine_word1 << ','
                << texenv << ',' << unsigned(glIsEnabled(GL_CULL_FACE)) << ',' << front << ',' << cull << ','
                << unsigned(glIsEnabled(GL_DEPTH_TEST)) << ',' << depthfunc << ',' << depthwrite << ','
                << unsigned(glIsEnabled(GL_ALPHA_TEST)) << ',' << unsigned(glIsEnabled(GL_BLEND)) << ','
                << v.x << ',' << v.y << ',' << v.z << ',' << v.w << ',' << point.x << ',' << point.y << ',' << point.ndc_z << ','
                << v.s << ',' << v.t << ',' << unsigned(v.attributes.r) << ',' << unsigned(v.attributes.g) << ','
                << unsigned(v.attributes.b) << ',' << unsigned(v.attributes.a) << ',' << unsigned(v.color.r) << ','
                << unsigned(v.color.g) << ',' << unsigned(v.color.b) << ',' << unsigned(v.color.a) << ',' << planes << ','
                << area << ',' << rgba(state.primitive_color) << ',' << rgba(state.environment_color) << '\n';
        }
    }

    void draw_diagnostic_hud(int width, int height) {
        using Clock = std::chrono::steady_clock;
        static auto sample_begin = Clock::now();
        static unsigned frames = 0;
        static double fps = 0;
        ++frames;
        const auto now = Clock::now();
        const double interval = std::chrono::duration<double>(now-sample_begin).count();
        if (interval >= 0.5) { fps=frames/interval; frames=0; sample_begin=now; }
        if (!g_diagnostic_hud || width<=0 || height<=0) return;
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glViewport(0,0,width,height);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST); glDisable(GL_LIGHTING);
        glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0,width,height,0,-1,1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        if (!g_hud_font) {
            HDC dc=wglGetCurrentDC();
            HFONT font=CreateFontA(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,"Consolas");
            HGDIOBJ previous=SelectObject(dc,font);
            const GLuint lists=glGenLists(96);
            if (lists && wglUseFontBitmapsA(dc,32,96,lists)) g_hud_font=lists;
            else if (lists) glDeleteLists(lists,96);
            SelectObject(dc,previous); DeleteObject(font);
        }
        const float panel_width=static_cast<float>(std::min(width-16,570));
        glColor4f(0.025F,0.04F,0.06F,0.92F);
        glBegin(GL_QUADS); glVertex2f(8,8); glVertex2f(panel_width+8,8);
        glVertex2f(panel_width+8,164); glVertex2f(8,164); glEnd();
        const auto area=::xr64::render_diagnostics::fit_4_3(width,height);
        std::array<std::string,8> lines;
        std::ostringstream timing; timing<<std::fixed<<std::setprecision(1)<<fps<<" PRESENT FPS | "
            <<std::chrono::duration<double,std::milli>(now-frame_begin_).count()<<" MS CPU SUBMIT";
        lines[0]="XR64 RENDER DIAGNOSTICS | "+std::to_string(memory_profile::kRdramSize / (1024*1024))+" MiB";
        lines[1]=timing.str();
        lines[2]="DRAWABLE "+std::to_string(width)+"x"+std::to_string(height)+" | GAME "+std::to_string(area.width)+"x"+std::to_string(area.height);
        lines[3]="TASK "+std::to_string(task_sequence_)+" | TRI "+std::to_string(raw_stats_.triangle_count)+" | RECT "+std::to_string(raw_stats_.rectangle_count)+" | UPLOAD "+std::to_string(texture_upload_count_);
        lines[4]="GL ERR "+std::to_string(g_gl_error_count)+" | READBACKS "+std::to_string(g_framebuffer_reads)+" | LEGACY "+(::xr64::render_diagnostics::legacy()?"ON":"OFF");
        lines[5]=diagnostic_task_selected()?"CAPTURING ONE TASK: MESH / MATERIAL / UV":"MESH CAPTURE: READY (F9)";
        lines[6]=std::string("FILTER NEAREST | MESH SHADE ")+(g_bypass_mesh_shading?"BYPASSED (DIAGNOSTIC)":"GAME") + (xr64_rw092_ai_freeze_enabled()?" | AI FREEZE ON":" | AI FREEZE OFF") + (reversal_request.load()?" | F10 REV PENDING":reversal_state.load()==1?" | F10 REV ON":reversal_state.load()==2?" | F10 REV BLOCKED":" | F10 REV OFF");
        lines[6] += g_diagnostic_fov>0.0F ? " | SHARED VFOV "+std::to_string(int(g_diagnostic_fov)) : " | FOV GAME";
        lines[7]="F1 HUD  F2 SIZE  F3 SHADE  F4 AI  F5 RESET FOV  F7/F8 FOV  F6 ASPECT  F9/BACK CAPTURE  F11 FULLSCREEN";
        if (g_hud_font) {
            glListBase(g_hud_font-32);
            for (std::size_t i=0;i<lines.size();++i) {
                glColor4f(i==0?0.3F:0.9F,0.95F,1,1);glRasterPos2i(18,27+static_cast<int>(i)*18);
                glCallLists(static_cast<GLsizei>(lines[i].size()),GL_UNSIGNED_BYTE,lines[i].data());
            }
        }
        glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glPopClientAttrib();glPopAttrib();
    }

    void set_triangle_draw_state(const ::xr64::N64RawFast3DDrawState& state) const {
        set_blend_depth(state);
        // Keep the pixel projection for the fixed-function fallback and for
        // later rectangles. The retained vertex shader writes host clip space.
        set_pixel_projection();
        if (state.textured && texture_id_ != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture_id_);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glFrontFace(GL_CCW);
        if (state.cull_front || state.cull_back) {
            glEnable(GL_CULL_FACE);
            glCullFace(state.cull_front && state.cull_back ? GL_FRONT_AND_BACK :
                    state.cull_front ? GL_FRONT : GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        set_triangle_material(state);
    }
#ifdef XR64_OPENXR
    static float xr_world_units() {
        float units=100;
        if(const char* value=std::getenv("XR64_XR_UNITS_PER_METRE")) {
            const float parsed=std::strtof(value,nullptr);if(std::isfinite(parsed)&&parsed>0)units=parsed;
        }
        return units;
    }
    bool draw_xr_retained_triangles(const ::xr64::N64RawFast3DVertex* vertices,
            std::size_t vertex_count, const ::xr64::N64RawFast3DDrawState& state) {
        if (!xr_retained_geometry_enabled() || !g_xr_eye || g_xr_menu_flat ||
                !frame_snapshot_ || !vertices || vertex_count == 0 ||
                !state.perspective_projection ||
                !(state.perspective_x_norm > 0.00001F) ||
                !(state.perspective_y_norm > 0.00001F) ||
                !(state.perspective_w_norm > 0.00001F) ||
                ::xr64::render_diagnostics::legacy() || diagnostic_task_selected())
            return false;
        std::size_t first = 0;
        const auto& frame = frame_snapshot_->frame;
        if (!frame.triangle_span_offset(vertices, vertex_count, first) ||
                frame.triangle_vertex_count() >
                    kTriangleVboByteLimit / sizeof(::xr64::N64RawFast3DVertex) ||
                first > static_cast<std::size_t>(std::numeric_limits<GLint>::max()) ||
                vertex_count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
            ++g_xr_retained_geometry.fallbacks;
            return false;
        }
        auto& path = g_xr_retained_geometry;
        if (!path.ensure()) { ++path.fallbacks; return false; }
        const auto& eye = *g_xr_eye;
        const float l = std::tan(eye.fov[0]), r = std::tan(eye.fov[1]);
        const float u = std::tan(eye.fov[2]), d = std::tan(eye.fov[3]);
        if (!std::isfinite(l) || !std::isfinite(r) ||
                !std::isfinite(u) || !std::isfinite(d) ||
                !(r - l > 0.00001F) || !(u - d > 0.00001F)) {
            ++path.fallbacks;
            return false;
        }
        for (float value : eye.position)
            if (!std::isfinite(value)) { ++path.fallbacks; return false; }
        for (float value : eye.orientation)
            if (!std::isfinite(value)) { ++path.fallbacks; return false; }
        const bool reuse = path.uploaded_snapshot == frame_snapshot_ &&
                path.uploaded_generation == frame_snapshot_->generation;
        if (!reuse) {
            // The CPU clipper discards invalid triangles. Keep that behavior on
            // this first shader candidate by falling back for the whole frame.
            for (std::size_t i = 0; i < frame.triangle_vertex_count(); ++i) {
                const auto& v = frame.triangle_vertex_data()[i];
                if (!std::isfinite(v.x) || !std::isfinite(v.y) ||
                        !std::isfinite(v.w)) {
                    ++path.fallbacks;
                    return false;
                }
            }
        }
        set_drawable_viewport();
        apply_guest_scissor();
        set_triangle_draw_state(state);
        path.bind_draw_scope();
        path.set_texcoord_array(state.textured);
        if (!reuse) {
            const auto bytes = frame.triangle_vertex_count() *
                    sizeof(::xr64::N64RawFast3DVertex);
            g_triangle_vbo.buffer_data(kGlArrayBuffer,
                    static_cast<std::ptrdiff_t>(bytes),
                    frame.triangle_vertex_data(), kGlStreamDraw);
            path.uploaded_snapshot = frame_snapshot_;
            path.uploaded_generation = frame_snapshot_->generation;
            ++path.uploads;
            path.upload_bytes += bytes;
        } else {
            ++path.reused_draws;
        }
        path.bind_program();
        float units = 100.0F;
        if (const char* value = std::getenv("XR64_XR_UNITS_PER_METRE")) {
            const float parsed = std::strtof(value, nullptr);
            if (std::isfinite(parsed) && parsed > 0.0F) units = parsed;
        }
        const float near_z = units * 0.02F, far_z = units * 1000.0F;
        path.send_3f(RetainedXrGeometryPath::FloatSlot::NormInv, path.u.norm_inv, 1.0F / state.perspective_x_norm,
                1.0F / state.perspective_y_norm,
                1.0F / state.perspective_w_norm);
        path.send_3f(RetainedXrGeometryPath::FloatSlot::EyePos, path.u.eye_pos, eye.position[0] * units,
                eye.position[1] * units, eye.position[2] * units);
        path.send_4f(RetainedXrGeometryPath::FloatSlot::Orientation, path.u.orientation, -eye.orientation[0],
                -eye.orientation[1], -eye.orientation[2], eye.orientation[3]);
        path.send_4f(RetainedXrGeometryPath::FloatSlot::Projection, path.u.projection, 2.0F / (r - l),
                (r + l) / (r - l), 2.0F / (u - d), (u + d) / (u - d));
        path.send_2f(RetainedXrGeometryPath::FloatSlot::Depth, path.u.depth, -(far_z + near_z) / (far_z - near_z),
                -2.0F * far_z * near_z / (far_z - near_z));
        path.send_4f(RetainedXrGeometryPath::FloatSlot::Viewport, path.u.viewport, state.viewport.scale_x,
                state.viewport.scale_y, state.viewport.translate_x,
                state.viewport.translate_y);
        path.send_2f(RetainedXrGeometryPath::FloatSlot::LogicalInv, path.u.logical_inv, 1.0F / (2.0F * logical_width_),
                1.0F / (2.0F * logical_height_));
        path.send_1i(RetainedXrGeometryPath::IntSlot::Affine, path.u.affine,
                state.textured && !(state.other_mode & (1ULL << 51U)) ? 1 : 0);
        const bool textured = state.textured && texture_id_ != 0;
        const int material = !textured ? 0 :
                ::xr64::n64_raw_fast3d_palette_lerp_material(state) ? 3 :
                ::xr64::n64_raw_fast3d_texture_rgb_material(state) ? 2 : 1;
        path.send_1i(RetainedXrGeometryPath::IntSlot::Material, path.u.material, material);
        path.send_1i(RetainedXrGeometryPath::IntSlot::Texture, path.u.texture, 0);
        path.send_1i(RetainedXrGeometryPath::IntSlot::BypassShading, path.u.bypass_shading, g_bypass_mesh_shading ? 1 : 0);
        const auto& primitive = state.primitive_color;
        path.send_4f(RetainedXrGeometryPath::FloatSlot::PrimitiveColor, path.u.primitive_color, primitive.r / 255.0F,
                primitive.g / 255.0F, primitive.b / 255.0F,
                primitive.a / 255.0F);
        const auto& environment = state.environment_color;
        path.send_4f(RetainedXrGeometryPath::FloatSlot::EnvironmentColor, path.u.environment_color, environment.r / 255.0F,
                environment.g / 255.0F, environment.b / 255.0F,
                environment.a / 255.0F);
        path.send_1f(RetainedXrGeometryPath::FloatSlot::AlphaScale, path.u.alpha_scale,
                ::xr64::n64_raw_fast3d_texture_alpha_scale(state));
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first),
                static_cast<GLsizei>(vertex_count));
        ++path.draws;
        if (path.draws % 30000U == 0U) path.write_metrics();
        note_triangle_submission(true);
        observe_framebuffer_change("triangle");
        return true;
    }
#endif
    void draw_triangles_vbo(const std::vector<::xr64::N64RawFast3DVertex> &vertices,
            const ::xr64::N64RawFast3DDrawState &state) {
        auto &packed = g_triangle_vbo.staging;
        packed.clear();
        packed.reserve(vertices.size());
        const bool perspective_uv = state.textured && (state.other_mode & (1ULL << 51U));
        const bool palette_lerp = ::xr64::n64_raw_fast3d_palette_lerp_material(state);
        for (const auto &vertex : vertices) {
            ::xr64::N64RawFast3DGuestPosition guest;
            if (!::xr64::n64_raw_fast3d_map_to_guest_viewport(
                        vertex, state.viewport, guest)) continue;
            TriangleArrayVertex out{};
            out.position[0] = guest.x;
            out.position[1] = guest.y;
            out.position[2] = ::xr64::n64_raw_fast3d_pixel_projection_z(guest.ndc_z);
            if (state.textured) {
                if (perspective_uv) {
                    const float q = 1.0F / vertex.w;
                    out.texcoord[0] = vertex.s * q;
                    out.texcoord[1] = vertex.t * q;
                    out.texcoord[2] = 0.0F;
                    out.texcoord[3] = q;
                } else {
                    out.texcoord[0] = vertex.s;
                    out.texcoord[1] = vertex.t;
                    out.texcoord[2] = 0.0F;
                    out.texcoord[3] = 1.0F;
                }
            }
            const auto &color = palette_lerp ? state.environment_color : vertex.color;
            out.color[0] = g_bypass_mesh_shading && !palette_lerp ? 255 : color.r;
            out.color[1] = g_bypass_mesh_shading && !palette_lerp ? 255 : color.g;
            out.color[2] = g_bypass_mesh_shading && !palette_lerp ? 255 : color.b;
            out.color[3] = color.a;
            packed.push_back(out);
        }
        if (packed.empty()) return;
        GLint previous_array_buffer = 0;
        glGetIntegerv(kGlArrayBufferBinding, &previous_array_buffer);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        g_triangle_vbo.bind_buffer(kGlArrayBuffer, g_triangle_vbo.buffer);
        g_triangle_vbo.buffer_data(kGlArrayBuffer,
                static_cast<std::ptrdiff_t>(packed.size() * sizeof(TriangleArrayVertex)),
                packed.data(), kGlStreamDraw);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, static_cast<GLsizei>(sizeof(TriangleArrayVertex)),
                reinterpret_cast<const GLvoid *>(offsetof(TriangleArrayVertex, position)));
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, static_cast<GLsizei>(sizeof(TriangleArrayVertex)),
                reinterpret_cast<const GLvoid *>(offsetof(TriangleArrayVertex, color)));
        if (state.textured) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(4, GL_FLOAT, static_cast<GLsizei>(sizeof(TriangleArrayVertex)),
                    reinterpret_cast<const GLvoid *>(offsetof(TriangleArrayVertex, texcoord)));
        } else {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(packed.size()));
        ++g_triangle_vbo.stream_draws;
        g_triangle_vbo.stream_upload_bytes += packed.size() *
                sizeof(TriangleArrayVertex);
        glPopClientAttrib();
        g_triangle_vbo.bind_buffer(kGlArrayBuffer,
                static_cast<GLuint>(previous_array_buffer));
    }
    void set_triangle_material(const ::xr64::N64RawFast3DDrawState &state) const {
        if (state.textured && texture_id_ != 0) {
            const GLint filter = ::xr64::n64_raw_fast3d_smooth_palette_lerp_material(state) ?
                    GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        }
        if (::xr64::n64_raw_fast3d_palette_lerp_material(state)) {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_CONSTANT);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1);
            const auto &c = state.primitive_color;
            const GLfloat color[4] = {c.r/255.0F,c.g/255.0F,c.b/255.0F,c.a/255.0F};
            glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);
            return;
        }
        if (!::xr64::n64_raw_fast3d_texture_rgb_material(state)) {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            return;
        }
        // GL_MODULATE incorrectly tints this texture-only RGB material with
        // G_VTX colors. Collapse the two alpha multiplies into one constant;
        // this does not emulate the RDP's intermediate fixed-point rounding.
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1);
        const GLfloat constant[4] = {1, 1, 1,
                ::xr64::n64_raw_fast3d_texture_alpha_scale(state)};
        glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);
        if (diagnostic_task_selected()) std::fprintf(stderr,
                "RW082_MATERIAL task=%llu rgb=texel0 alpha=texel0*primitive*environment alpha_scale=%.6f\n",
                static_cast<unsigned long long>(task_sequence_), constant[3]);
    }

    static void set_blend_depth(const ::xr64::N64RawFast3DDrawState &state) {
        if (state.alpha_blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        if (state.depth_test) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(state.depth_write ? GL_TRUE : GL_FALSE);
    }

#if XR64_RENDER_DIAGNOSTICS
    bool diagnostic_task_selected() const {
        const char *value = std::getenv("XR64_RW_DIAG_TASK");
        if (value == nullptr || value[0] == '\0') return false;
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 10);
        return end != value && *end == '\0' && parsed == task_sequence_;
    }
#else
    bool diagnostic_task_selected() const { return ::xr64::render_diagnostics::capture_selected(task_sequence_); }
#endif

    void set_clip_projection() const {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1.0, 1.0, 1.0, -1.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void set_pixel_projection() const {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, logical_width_, logical_height_, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }
};

} // namespace

bool RageWarsDesktopRenderer::open_controller(int device_index) {
    if (controller_ != nullptr || g_sdl.is_game_controller(device_index) != SDL_TRUE) {
        return false;
    }
    SDL_GameController *controller = g_sdl.game_controller_open(device_index);
    const char *enumerated_name = g_sdl.game_controller_name_for_index(device_index);
    if (controller == nullptr) {
        std::fprintf(stderr,
                "RW077_SDL_CONTROLLER_OPEN index=%d name=%s success=no error=%s\n",
                device_index, enumerated_name != nullptr ? enumerated_name : "unknown",
                sdl_error());
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        return false;
    }
    SDL_Joystick *joystick = g_sdl.game_controller_get_joystick(controller);
    controller_ = controller;
    controller_instance_id_ = joystick != nullptr
            ? g_sdl.joystick_instance_id(joystick) : -1;
    const char *opened_name = g_sdl.game_controller_name(controller);
    {
        std::lock_guard lock(controller_mutex_);
        controller_snapshot_ = {};
        latched_buttons_ = 0;
        controller_snapshot_.connected = true;
        controller_snapshot_.device_name = opened_name != nullptr
                ? opened_name : (enumerated_name != nullptr ? enumerated_name : "unknown");
        ++controller_snapshot_.sequence;
    }
    std::fprintf(stderr,
            "RW077_SDL_CONTROLLER_OPEN index=%d instance=%d name=%s success=yes mapping=standard\n",
            device_index, controller_instance_id_,
            opened_name != nullptr ? opened_name : "unknown");
    XR64_RENDER_DIAGNOSTIC_FLUSH();
    refresh_controller_snapshot();
    return true;
}

void RageWarsDesktopRenderer::close_controller() {
    if (controller_ == nullptr) return;
    std::string name;
    {
        std::lock_guard lock(controller_mutex_);
        name = controller_snapshot_.device_name;
        const std::uint64_t next_sequence = controller_snapshot_.sequence + 1U;
        gamepad_input_ = {};
        menu_gamepad_input_ = {};
        gamepad_state_ = {};
        gamepad_mapper_.reset();
        controller_snapshot_.sequence = next_sequence;
    }
    g_sdl.game_controller_close(static_cast<SDL_GameController *>(controller_));
    controller_ = nullptr;
    controller_instance_id_ = -1;
    std::fprintf(stderr, "RW077_SDL_CONTROLLER_REMOVED name=%s state=neutral\n",
            name.empty() ? "unknown" : name.c_str());
    XR64_RENDER_DIAGNOSTIC_FLUSH();
}

void RageWarsDesktopRenderer::refresh_controller_snapshot() {
    SDL_GameController *controller = static_cast<SDL_GameController *>(controller_);
    if (controller == nullptr) return;
    if (g_sdl.game_controller_get_attached(controller) != SDL_TRUE) {
        close_controller();
        return;
    }

    g_sdl.game_controller_update();
    const auto pressed = [controller](SDL_GameControllerButton button) {
        return g_sdl.game_controller_get_button(controller, button) != 0;
    };
    StandardGamepadState physical;
    for(unsigned button=0;button<15;++button)
        if(pressed(static_cast<SDL_GameControllerButton>(button)))physical.raw_buttons|=1U<<button;
    physical.left_trigger=g_sdl.game_controller_get_axis(controller,SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    physical.start = pressed(SDL_CONTROLLER_BUTTON_START);
    physical.south = pressed(SDL_CONTROLLER_BUTTON_A);
    physical.east = pressed(SDL_CONTROLLER_BUTTON_B);
    physical.dpad_up = pressed(SDL_CONTROLLER_BUTTON_DPAD_UP);
    physical.dpad_down = pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    physical.dpad_left = pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    physical.dpad_right = pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    physical.left_shoulder = pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    physical.right_shoulder = pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    physical.left_x = g_sdl.game_controller_get_axis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    physical.left_y = g_sdl.game_controller_get_axis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    physical.right_x = g_sdl.game_controller_get_axis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    physical.right_y = g_sdl.game_controller_get_axis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
    physical.right_trigger = g_sdl.game_controller_get_axis(
            controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    {
        std::lock_guard lock(controller_mutex_);
        gamepad_state_ = physical;
        menu_gamepad_input_ = map_gamepad_menu(physical);
        gamepad_input_ = gamepad_mapper_.map(physical, control_settings_, true);
    }
}

void RageWarsDesktopRenderer::set_mouse_capture(bool captured) {
    if (mouse_captured_ == captured) return;
    if (g_sdl.set_relative_mouse_mode(captured ? SDL_TRUE : SDL_FALSE) != 0) {
        std::fprintf(stderr,
                "RW101_MOUSE_CAPTURE enabled=%u success=no error=%s\n",
                captured ? 1U : 0U, sdl_error());
        XR64_RENDER_DIAGNOSTIC_FLUSH();
        return;
    }
    mouse_captured_ = captured;
    local_presentation().mouse_context(false);
    {
        std::lock_guard lock(controller_mutex_);
        mouse_aim_x_ = mouse_aim_y_ = 0.0F;
        mouse_motion_.clear();
        relative_mouse_x_ = relative_mouse_y_ = 0.0F;
        suppress_mouse_buttons_ = captured;
    }
    int discard_x = 0;
    int discard_y = 0;
    g_sdl.get_relative_mouse_state(&discard_x, &discard_y);
    std::fprintf(stderr, "RW101_MOUSE_CAPTURE enabled=%u success=yes\n",
            captured ? 1U : 0U);
    XR64_RENDER_DIAGNOSTIC_FLUSH();
}

void RageWarsDesktopRenderer::set_input_focus(bool focused) {
    local_presentation().mouse_context(false);
    std::lock_guard lock(controller_mutex_);
    window_focused_ = focused;
    mouse_motion_.clear();
    mouse_aim_x_ = mouse_aim_y_ = 0.0F;
    relative_mouse_x_ = relative_mouse_y_ = 0.0F;
    mouse_activity_distance_ = 0.0F;
    latched_buttons_ = menu_wheel_buttons_ = 0;
    keyboard_wheel_input_ = {};
    pending_weapon_cycle_=last_weapon_cycle_=0;
    last_keyboard_buttons_ = 0;
    keyboard_input_ = menu_keyboard_input_ = {};
    direct_stick_ = {};
    gamepad_mapper_.reset();
    controller_snapshot_.buttons = 0;
    controller_snapshot_.stick_x = controller_snapshot_.stick_y = 0.0F;
}

void RageWarsDesktopRenderer::refresh_keyboard_mouse_snapshot() {
    int key_count = 0;
    const Uint8 *keys = g_sdl.get_keyboard_state(&key_count);
    const auto down = [keys, key_count](SDL_Scancode key) {
        return keys != nullptr && static_cast<int>(key) < key_count && keys[key] != 0;
    };

    int relative_x = 0;
    int relative_y = 0;
    const Uint32 mouse_buttons = g_sdl.get_relative_mouse_state(&relative_x, &relative_y);
    KeyboardMouseState physical;
    physical.move_forward = down(SDL_SCANCODE_W);
    physical.move_back = down(SDL_SCANCODE_S);
    physical.move_left = down(SDL_SCANCODE_A);
    physical.move_right = down(SDL_SCANCODE_D);
    physical.aim_up = down(SDL_SCANCODE_UP);
    physical.aim_down = down(SDL_SCANCODE_DOWN);
    physical.aim_left = down(SDL_SCANCODE_LEFT);
    physical.aim_right = down(SDL_SCANCODE_RIGHT);
    physical.jump = down(SDL_SCANCODE_SPACE);
    physical.action = down(SDL_SCANCODE_R) ||
            (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    physical.fire = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    physical.confirm = down(SDL_SCANCODE_RETURN);
    physical.start = down(SDL_SCANCODE_ESCAPE);
    physical.previous_weapon = down(SDL_SCANCODE_Q);
    physical.next_weapon = down(SDL_SCANCODE_E);
    physical.dpad_up = down(SDL_SCANCODE_I);
    physical.dpad_down = down(SDL_SCANCODE_K);
    physical.dpad_left = down(SDL_SCANCODE_J);
    physical.dpad_right = down(SDL_SCANCODE_L);

    std::lock_guard lock(controller_mutex_);
    static RelativeControlSample control_sample;
    if(control_sample_enabled() && !xr_enabled()) {
        const auto player=local_presentation().acquire().player;
        const auto counts=control_sample.next(player.valid && mouse_captured_ && window_focused_ &&
            gameplay_context_ && control_settings_.profile==controls::Profile::Modern,control_settings_);
        relative_x+=int(counts.x);relative_y+=int(counts.y);
        if(counts.x || counts.y)std::fprintf(stderr,"RW_LOCAL_SAMPLE_INPUT step=%u dx=%.0f dy=%.0f sx=%.4f sy=%.4f ix=%d iy=%d\n",
            control_sample.step,counts.x,counts.y,control_settings_.mouse_sensitivity_x,control_settings_.mouse_sensitivity_y,
            int(control_settings_.mouse_invert_x),int(control_settings_.mouse_invert_y));
    }
    if (suppress_mouse_buttons_) {
        suppress_mouse_buttons_ = mouse_buttons != 0;
        physical.fire = false;
        physical.action = down(SDL_SCANCODE_R);
    }
    menu_keyboard_input_=window_focused_ ? map_keyboard_menu(physical) : N64ControllerInput{};
    keyboard_input_=window_focused_ ? bindings::map(control_settings_.keyboard_bindings,[&](bindings::Code code) {
        if(code>=4 && code<512)return code<key_count && keys && keys[code]!=0;
        if(code>=513 && code<=517)return !suppress_mouse_buttons_ && (mouse_buttons&SDL_BUTTON(code-512))!=0;
        return false;
    }) : N64ControllerInput{};
    // Escape always remains a route back to the pause menu.
    if(window_focused_ && down(SDL_SCANCODE_ESCAPE))keyboard_input_.buttons|=kN64ButtonStart;
    keyboard_neutral_=mouse_buttons==0;
    if(keys)for(int key=0;key<key_count;++key)if(keys[key]){keyboard_neutral_=false;break;}

    relative_mouse_x_ = mouse_captured_ && window_focused_ ? static_cast<float>(relative_x) : 0.0F;
    relative_mouse_y_ = mouse_captured_ && window_focused_ ? static_cast<float>(relative_y) : 0.0F;
    // An uncaptured mouse can take over gameplay from a held controller.
    if (window_focused_ && gameplay_context_)
        mouse_activity_distance_ += std::abs(relative_x) + std::abs(relative_y);
    keyboard_activity_ = (keyboard_input_.buttons & static_cast<std::uint16_t>(~last_keyboard_buttons_)) != 0 ||
            keyboard_input_.stick_x != last_keyboard_axis_x_ || keyboard_input_.stick_y != last_keyboard_axis_y_ ||
            keyboard_wheel_input_.buttons!=0 || keyboard_wheel_input_.stick_x!=0 ||
            keyboard_wheel_input_.stick_y!=0 || keyboard_wheel_input_.weapon_cycle!=0 ||
            keyboard_input_.weapon_cycle!=last_keyboard_cycle_ || mouse_activity_distance_ >= 3.0F;
    if (mouse_activity_distance_ >= 3.0F) mouse_activity_distance_ = 0.0F;
    last_keyboard_buttons_ = keyboard_input_.buttons;
    last_keyboard_cycle_=keyboard_input_.weapon_cycle;
    last_keyboard_axis_x_ = keyboard_input_.stick_x;
    last_keyboard_axis_y_ = keyboard_input_.stick_y;
    if (mouse_captured_ && window_focused_) {
        mouse_aim_x_ = std::clamp(mouse_aim_x_ +
                static_cast<float>(relative_x) * mouse_sensitivity_, -1.0F, 1.0F);
        mouse_aim_y_ = std::clamp(mouse_aim_y_ -
                static_cast<float>(relative_y) * mouse_sensitivity_, -1.0F, 1.0F);
    } else {
        mouse_aim_x_ = 0.0F;
        mouse_aim_y_ = 0.0F;
    }
}

void RageWarsDesktopRenderer::publish_input_snapshot() {
    const bool capture_blocked=port_options::binding_input_blocked();
    bool log_transition = false;
    DesktopControllerSnapshot published;
    controls::DeviceActivity touch_activity;
#ifdef XR64_OPENXR
    if(xr_enabled())touch_activity=xr_device_activity(g_xr.input_snapshot());
#endif
    {
        std::lock_guard lock(controller_mutex_);
        const bool input_allowed=window_focused_ && !capture_blocked && !binding_pump_blocked_;
        if(!input_allowed) {
            latched_buttons_=menu_wheel_buttons_=0;
            keyboard_wheel_input_={};pending_weapon_cycle_=last_weapon_cycle_=0;mouse_motion_.clear();mouse_aim_x_=mouse_aim_y_=0;
        }
        N64ControllerInput keyboard_mouse=gameplay_context_
            ? merge_controller_input(keyboard_input_,keyboard_wheel_input_) : menu_keyboard_input_;
        if(!gameplay_context_)keyboard_mouse.buttons |= menu_wheel_buttons_;
        if(gameplay_context_ && control_settings_.profile!=controls::Profile::Modern) {
            keyboard_mouse.stick_x = std::clamp(keyboard_mouse.stick_x + mouse_aim_x_, -1.0F, 1.0F);
            keyboard_mouse.stick_y = std::clamp(keyboard_mouse.stick_y + mouse_aim_y_, -1.0F, 1.0F);
        }
        const auto selected = device_selector_.update(control_settings_.device_mode,
                controller_ != nullptr, gamepad_state_, keyboard_activity_, window_focused_, touch_activity);
#ifdef XR64_OPENXR
        g_xr_hand_motion_active.store(selected == controls::Device::Touch &&
            control_settings_.xr_motion_controls && touch_activity.available && touch_activity.focused,
            std::memory_order_release);
#endif
        if (selected != active_device_) {
            latched_buttons_ = 0;
            mouse_motion_.clear();
            active_device_ = selected;
            pending_weapon_cycle_=last_weapon_cycle_=0;
        }
        const bool direct_mouse=input_allowed && gameplay_context_ && selected==controls::Device::KeyboardMouse &&
            control_settings_.profile==controls::Profile::Modern;
        local_presentation().mouse_context(direct_mouse);
        if (input_allowed && gameplay_context_ && selected == controls::Device::KeyboardMouse) {
            if(direct_mouse)local_presentation().add_mouse(relative_mouse_x_,relative_mouse_y_,control_settings_);
            else mouse_motion_.add(relative_mouse_x_, relative_mouse_y_);
        } else {
            mouse_motion_.clear();
        }
        relative_mouse_x_ = relative_mouse_y_ = 0.0F;
        const auto& selected_pad = gameplay_context_ ? gamepad_input_ : menu_gamepad_input_;
        const N64ControllerInput mapped = controls::route_input(
                selected, selected_pad, keyboard_mouse, gameplay_context_, input_allowed);
        pending_weapon_cycle_ |= mapped.weapon_cycle & ~last_weapon_cycle_;
        last_weapon_cycle_=mapped.weapon_cycle;
        const auto keyboard_aim=merge_controller_input(keyboard_input_,keyboard_wheel_input_);
        direct_stick_ = selected == controls::Device::Controller
                ? controls::AxisPair{gamepad_input_.stick_x, gamepad_input_.stick_y}
                : controls::AxisPair{keyboard_aim.stick_x, keyboard_aim.stick_y};
        if(!input_allowed)direct_stick_={};
        const bool old_stick_active = std::hypot(
                controller_snapshot_.stick_x, controller_snapshot_.stick_y) > 0.05F;
        const bool new_stick_active = std::hypot(mapped.stick_x, mapped.stick_y) > 0.05F;
        log_transition = controller_snapshot_.buttons != mapped.buttons ||
                old_stick_active != new_stick_active;
        latched_buttons_ |= static_cast<std::uint16_t>(
                mapped.buttons & static_cast<std::uint16_t>(~controller_snapshot_.buttons));
        controller_snapshot_.connected = true;
        controller_snapshot_.buttons = mapped.buttons;
        controller_snapshot_.stick_x = mapped.stick_x;
        controller_snapshot_.stick_y = mapped.stick_y;
        controller_snapshot_.device_name = selected == controls::Device::Controller
                ? "Controller" : selected == controls::Device::Touch ? "OpenXR Touch" : "Keyboard + Mouse";
        ++controller_snapshot_.sequence;
        published = controller_snapshot_;
    }
    if (log_transition) {
        std::fprintf(stderr,
                "RW101_PC_INPUT buttons=0x%04X stick=(%.3f,%.3f) device=%s\n",
                published.buttons, published.stick_x, published.stick_y,
                published.device_name.c_str());
        XR64_RENDER_DIAGNOSTIC_FLUSH();
    }
}

bool RageWarsDesktopRenderer::pump_events() {
    if (!stats_.initialized) return true;
    const auto port_settings = port_options::snapshot();
    {
        std::lock_guard lock(controller_mutex_);
        if (!(control_settings_ == port_settings.controls)) {
            local_presentation().mouse_context(false);
            control_settings_ = port_settings.controls;
            gamepad_mapper_.reset();
            mouse_motion_.clear();
            mouse_aim_x_ = mouse_aim_y_ = 0.0F;
            latched_buttons_ = menu_wheel_buttons_ = 0;
            keyboard_wheel_input_ = {};
            pending_weapon_cycle_=last_weapon_cycle_=0;
        }
    }
    if (port_options::revision() != g_port_options_revision) {
        g_diagnostic_fov = port_settings.vertical_fov_degrees;
        g_widescreen = port_settings.widescreen;
        SDL_Window *settings_window = static_cast<SDL_Window *>(window_);
        if (settings_window != nullptr) {
            static constexpr int sizes[][2] = {
                    {960, 720}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
            g_sdl.set_window_fullscreen(settings_window,
                    port_settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            if (!port_settings.fullscreen) {
                const auto preset = std::min<std::uint8_t>(port_settings.window_size_preset,
                        port_options::kWindowPresetCount - 1);
                g_sdl.set_window_size(settings_window, sizes[preset][0], sizes[preset][1]);
            }
        }
        g_port_options_revision = port_options::revision();
    }
    bool capture_during_pump=port_options::binding_input_blocked();
    SDL_Event event{};
    while (g_sdl.poll_event(&event) != 0) {
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture)
            audio::device_removed(event.adevice.which);
        if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            port_options::cancel_binding_capture();
            set_input_focus(false);
            set_mouse_capture(false);
        }
        if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            set_input_focus(true);
        }
        if(port_options::binding_input_blocked()) {
            capture_during_pump=true;
            using bindings::Device;
            if(event.type==SDL_KEYDOWN && !event.key.repeat) {
                if(event.key.keysym.scancode==SDL_SCANCODE_ESCAPE)port_options::cancel_binding_capture();
                else port_options::capture_binding_input(Device::KeyboardMouse,static_cast<bindings::Code>(event.key.keysym.scancode));
            } else if(event.type==SDL_MOUSEBUTTONDOWN) {
                port_options::capture_binding_input(Device::KeyboardMouse,bindings::kMouseBase+event.button.button);
            } else if(event.type==SDL_MOUSEWHEEL && event.wheel.y) {
                const int y=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
                port_options::capture_binding_input(Device::KeyboardMouse,y>0 ? bindings::kWheelUp : bindings::kWheelDown);
            } else if(event.type==SDL_CONTROLLERBUTTONDOWN && event.cbutton.which==controller_instance_id_) {
                if(event.cbutton.button==SDL_CONTROLLER_BUTTON_BACK)port_options::cancel_binding_capture();
                else port_options::capture_binding_input(Device::Controller,bindings::kPadBase+event.cbutton.button);
            } else if(event.type==SDL_CONTROLLERAXISMOTION && event.caxis.which==controller_instance_id_ &&
                    event.caxis.value>=kControllerDigitalAxisThreshold) {
                if(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT)
                    port_options::capture_binding_input(Device::Controller,bindings::kLeftTrigger);
                if(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                    port_options::capture_binding_input(Device::Controller,bindings::kRightTrigger);
            }
            if(event.type==SDL_KEYDOWN || event.type==SDL_KEYUP ||
               event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP ||
               event.type==SDL_MOUSEWHEEL || event.type==SDL_MOUSEMOTION ||
               event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_CONTROLLERBUTTONUP ||
               event.type==SDL_CONTROLLERAXISMOTION)continue;
        }
        if (event.type == SDL_MOUSEWHEEL) {
            std::lock_guard lock(controller_mutex_);
            const int y=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            if(y) {
                const auto code=y>0 ? bindings::kWheelUp : bindings::kWheelDown;
                const auto pulse=bindings::map(control_settings_.keyboard_bindings,[&](bindings::Code candidate){return candidate==code;});
                keyboard_wheel_input_=merge_controller_input(keyboard_wheel_input_,pulse);
                menu_wheel_buttons_|=y>0 ? kN64ButtonL : kN64ButtonR;
            }
        }
#ifndef XR64_DEMO_BUILD
        if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                event.cbutton.which == controller_instance_id_ &&
                event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK &&
                !xr_enabled()) {
            g_capture_next_task = true;
            std::fprintf(stderr, "RW090_CAPTURE_REQUEST source=controller-back\n");
        }

#endif
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            if (event.key.keysym.sym == SDLK_ESCAPE) set_mouse_capture(false);
#ifdef XR64_OPENXR
            if(event.key.keysym.sym==SDLK_F12&&!event.key.repeat)g_xr.request_recenter();
#endif
#ifndef XR64_DEMO_BUILD
            if (event.key.keysym.sym == SDLK_b && (event.key.keysym.mod & KMOD_CTRL)) xr64_body_request_toggle();
            if (event.key.keysym.sym == SDLK_F5) {
                port_options::set_vertical_fov(port_options::kFovGameOriginal);
            }
            if (event.key.keysym.sym == SDLK_F7 || event.key.keysym.sym == SDLK_F8) {
                const float current=g_diagnostic_fov>0.0F?g_diagnostic_fov:75.0F;
                g_diagnostic_fov=std::clamp(current+(event.key.keysym.sym==SDLK_F8?5.0F:-5.0F),40.0F,110.0F);
                port_options::set_vertical_fov(g_diagnostic_fov);
                std::fprintf(stderr,"RW096_FLATSCREEN_FOV vertical_degrees=%.1f\n",g_diagnostic_fov);
            }
            if (event.key.keysym.sym == SDLK_F6) {
                g_widescreen = !g_widescreen;
                port_options::set_widescreen(g_widescreen);
                std::fprintf(stderr,"RW095_ASPECT widescreen=%u\n",g_widescreen?1U:0U);
            }
            if (event.key.keysym.sym == SDLK_F10 && !event.key.repeat) {
                reversal_request.store(true, std::memory_order_relaxed);
                std::fprintf(stderr, "RW095_DAMAGE_REVERSAL toggle_requested=1\n");
            }
            if (event.key.keysym.sym == SDLK_F4) {
                const bool freeze = !rw092_ai_freeze.load(std::memory_order_relaxed);
                rw092_ai_freeze.store(freeze, std::memory_order_relaxed);
                std::fprintf(stderr, "RW092_AI_FREEZE enabled=%d\n", freeze ? 1 : 0);
            }
            if (event.key.keysym.sym == SDLK_F1) g_diagnostic_hud = !g_diagnostic_hud;
            if (event.key.keysym.sym == SDLK_F3) {
                g_bypass_mesh_shading = !g_bypass_mesh_shading;
                std::fprintf(stderr, "RW085_SHADING_TOGGLE bypass=%u\n", g_bypass_mesh_shading?1U:0U);
            }
            if (event.key.keysym.sym == SDLK_F9 && !event.key.repeat) {
                if (xr_enabled()) {
                    g_capture_next_xr_frame = true;
                    const char *memory_capture = std::getenv("XR64_RW_MEMORY_CAPTURE");
                    if (memory_capture && std::string(memory_capture) == "1")
                        g_capture_next_memory_task = true;
                } else g_capture_next_task = true;
                std::fprintf(stderr, "RW111_CAPTURE_KEY mode=%s\n", xr_enabled() ? "xr-frame" : "task");
            }
            SDL_Window *window = static_cast<SDL_Window *>(window_);
            if (window && event.key.keysym.sym == SDLK_F2) {
                const int sizes[][2] = {{960,720},{1280,720},{1920,1080},{2560,1440},{3840,2160}};
                const unsigned preset = (port_options::snapshot().window_size_preset + 1) % 5;
                g_sdl.set_window_fullscreen(window, 0);
                g_sdl.set_window_size(window, sizes[preset][0], sizes[preset][1]);
                port_options::set_fullscreen(false);
                port_options::set_window_size_preset(static_cast<std::uint8_t>(preset));
            }
#else
            SDL_Window *window = static_cast<SDL_Window *>(window_);
#endif
            if (window && event.key.keysym.sym == SDLK_F11) {
                const bool full = (g_sdl.get_window_flags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                g_sdl.set_window_fullscreen(window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                port_options::set_fullscreen(!full);
            }
        }
        if (event.type == SDL_CONTROLLERDEVICEREMOVED &&
                event.cdevice.which == controller_instance_id_) {
            if(port_options::binding_editor_snapshot().device==bindings::Device::Controller)
                port_options::cancel_binding_capture();
            close_controller();
        } else if (event.type == SDL_CONTROLLERDEVICEADDED && controller_ == nullptr) {
            open_controller(event.cdevice.which);
        }
    }
    if (controller_ == nullptr) {
        const int device_count = g_sdl.num_joysticks();
        for (int index = 0; index < device_count && controller_ == nullptr; ++index) {
            if (g_sdl.is_game_controller(index) == SDL_TRUE) open_controller(index);
        }
    }
    refresh_controller_snapshot();
    refresh_keyboard_mouse_snapshot();
    bool neutral,focused;
    {
        std::lock_guard lock(controller_mutex_);
        binding_pump_blocked_=capture_during_pump;
        focused=window_focused_;
        const auto& pad=gamepad_state_;
        neutral=keyboard_neutral_ && bindings::pad_buttons(pad)==0 &&
            pad.left_trigger<kControllerDigitalAxisThreshold && pad.right_trigger<kControllerDigitalAxisThreshold &&
            std::abs(int(pad.left_x))<6554 && std::abs(int(pad.left_y))<6554 &&
            std::abs(int(pad.right_x))<6554 && std::abs(int(pad.right_y))<6554;
    }
    if(port_options::binding_editor_snapshot().device!=bindings::Device::XR)
        port_options::poll_binding_capture(neutral,focused);
    publish_input_snapshot();
    bool capture_mouse=false;
    {
        std::lock_guard lock(controller_mutex_);
        capture_mouse=window_focused_ && gameplay_context_ &&
            active_device_==controls::Device::KeyboardMouse && !binding_pump_blocked_;
    }
    set_mouse_capture(capture_mouse && !port_options::binding_input_blocked());
    return true;
}

void RageWarsDesktopRenderer::request_xr_transition(XrTransitionRequest request) {
#ifdef XR64_OPENXR
    publish_xr_transition_request(request);
#else
    (void)request;
#endif
}

XrPresentationMode RageWarsDesktopRenderer::xr_presentation_mode() const {
#ifdef XR64_OPENXR
    return g_xr_mode.load(std::memory_order_acquire);
#else
    return XrPresentationMode::Desktop;
#endif
}

std::uint64_t RageWarsDesktopRenderer::xr_transition_revision() const {
#ifdef XR64_OPENXR
    return g_xr_transition_revision.load(std::memory_order_acquire);
#else
    return 0;
#endif
}

std::string RageWarsDesktopRenderer::xr_transition_status() const {
#ifdef XR64_OPENXR
    std::lock_guard lock(g_xr_status_mutex);
    return g_xr_transition_status;
#else
    return "OpenXR support is not compiled";
#endif
}

bool gate5_xr_auto_ready_hint(std::string& reason) {
#ifdef XR64_OPENXR
    return passive_steamvr_ready_hint(reason);
#else
    reason="OpenXR support is not compiled";
    return false;
#endif
}

XrMotionInput gate5_xr_motion_snapshot() {
#ifdef XR64_OPENXR
    return xr_enabled() && g_xr_hand_motion_active.load(std::memory_order_acquire)
        ? g_xr.input_snapshot() : XrMotionInput{};
#else
    return {};
#endif
}

XrMotionInput gate5_xr_shot_snapshot() {
#ifdef XR64_OPENXR
    return xr_enabled() && g_xr_hand_motion_active.load(std::memory_order_acquire)
        ? g_xr.shot_snapshot() : XrMotionInput{};
#else
    return {};
#endif
}

DesktopControllerSnapshot RageWarsDesktopRenderer::controller_snapshot() const {
    std::lock_guard lock(controller_mutex_);
    return controller_snapshot_;
}

DesktopControllerSnapshot RageWarsDesktopRenderer::consume_controller_snapshot(bool gameplay, bool xr_gameplay) {
    N64ControllerInput touch_input;
    bool touch_ready=false;
#ifdef XR64_OPENXR
    if (xr_enabled()) {
        gameplay=xr_gameplay;
        const auto touch=g_xr.input_snapshot();
        touch_ready=touch.available && touch.focused;
        const auto editor=port_options::binding_editor_snapshot();
        const bool was_blocked=port_options::binding_input_blocked();
        if(editor.device==bindings::Device::XR && was_blocked) {
            bool neutral=true;
            for(auto code=bindings::kXrBase;code<=bindings::kXrLast;++code)
                if(xr_binding_value(touch,code)>0.18F)neutral=false;
            port_options::poll_binding_capture(neutral,touch_ready);
            if(touch.menu_button)port_options::cancel_binding_capture();
            else if(port_options::binding_editor_snapshot().stage==bindings::CaptureStage::Listening)
                for(auto code=bindings::kXrBase;code<=bindings::kXrLast;++code)
                    if(xr_binding_value(touch,code)>=0.55F && port_options::capture_binding_input(bindings::Device::XR,code))break;
        }
        touch_input=g_motion_mapper.map(touch,gameplay,false,port_options::snapshot().controls);
        if(gameplay && port_options::snapshot().controls.xr_motion_controls &&
            !xr_shot_pose_usable(g_xr.shot_snapshot(),std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()))touch_input.buttons&=~kN64ButtonZ;
        if(was_blocked)touch_input={};
    }
#endif
    const bool capture_blocked=port_options::binding_input_blocked();
    std::lock_guard lock(controller_mutex_);
    const bool blocked=capture_blocked || binding_pump_blocked_;
    const bool input_allowed=window_focused_ && !blocked;
    if(!input_allowed) {
        latched_buttons_=menu_wheel_buttons_=0;
        keyboard_wheel_input_={};pending_weapon_cycle_=last_weapon_cycle_=0;
        mouse_motion_.clear();mouse_aim_x_=mouse_aim_y_=0;
    }
    if (gameplay_context_ != gameplay) {
        gameplay_context_ = gameplay;
        mouse_motion_.clear();
        mouse_aim_x_ = mouse_aim_y_ = 0.0F;
        latched_buttons_ = menu_wheel_buttons_ = 0;
        keyboard_wheel_input_ = {};
        pending_weapon_cycle_=last_weapon_cycle_=0;
    }
    N64ControllerInput keyboard_mouse=gameplay
        ? merge_controller_input(keyboard_input_,keyboard_wheel_input_) : menu_keyboard_input_;
    if(!gameplay)keyboard_mouse.buttons |= menu_wheel_buttons_;
    auto pad = gameplay ? gamepad_input_ : menu_gamepad_input_;
    if (gameplay) {
        const auto& settings = control_settings_;
        if(settings.profile!=controls::Profile::Modern) {
            keyboard_mouse.stick_x = std::clamp(keyboard_mouse.stick_x + mouse_aim_x_, -1.0F, 1.0F);
            keyboard_mouse.stick_y = std::clamp(keyboard_mouse.stick_y + mouse_aim_y_, -1.0F, 1.0F);
        }
        pad.stick_x *= settings.controller_sensitivity_x * (settings.controller_invert_x ? -1.0F : 1.0F);
        pad.stick_y *= settings.controller_sensitivity_y * (settings.controller_invert_y ? -1.0F : 1.0F);
        keyboard_mouse.stick_x *= settings.mouse_sensitivity_x * (settings.mouse_invert_x ? -1.0F : 1.0F);
        keyboard_mouse.stick_y *= settings.mouse_sensitivity_y * (settings.mouse_invert_y ? -1.0F : 1.0F);
    }
    auto mapped = controls::route_input(active_device_, pad, keyboard_mouse, gameplay, input_allowed);
    if(!blocked && touch_ready) {
        if(!gameplay)mapped=merge_controller_input(mapped,touch_input);
        else if(active_device_==controls::Device::Touch) {
            const auto pause=mapped.buttons & kN64ButtonStart;
            mapped=touch_input; mapped.buttons|=pause;
        } else mapped.buttons|=touch_input.buttons & kN64ButtonStart;
    }
    DesktopControllerSnapshot result = controller_snapshot_;
    result.buttons = mapped.buttons | (input_allowed ? latched_buttons_ : 0);
    result.stick_x = std::clamp(mapped.stick_x, -1.0F, 1.0F);
    result.stick_y = std::clamp(mapped.stick_y, -1.0F, 1.0F);
    latched_buttons_ = menu_wheel_buttons_ = 0;
    keyboard_wheel_input_ = {};
    mouse_aim_x_ = mouse_aim_y_ = 0.0F;
    return result;
}

controls::LookSample RageWarsDesktopRenderer::consume_look_sample() {
    XrMotionInput touch;
#ifdef XR64_OPENXR
    if(xr_enabled())touch=g_xr.input_snapshot();
#endif
    const bool capture_blocked=port_options::binding_input_blocked();
    std::lock_guard lock(controller_mutex_);
    const auto mouse = mouse_motion_.take();
    controls::LookSample sample=local_presentation().take_mouse();
    sample.device = active_device_;
    if(active_device_==controls::Device::Touch) {
#ifdef XR64_OPENXR
        const auto mapped=g_motion_mapper.map(touch,gameplay_context_,false,control_settings_);
        sample.stick={mapped.stick_x,mapped.stick_y};
#endif
        sample.focused=touch.available && touch.focused;
    } else {
        sample.stick = direct_stick_;
        sample.mouse_x = mouse.x;
        sample.mouse_y = mouse.y;
        sample.focused=window_focused_;
    }
    sample.focused=sample.focused && gameplay_context_ && !capture_blocked && !binding_pump_blocked_;
    return sample;
}

std::uint8_t RageWarsDesktopRenderer::consume_weapon_cycle() {
    const bool blocked=port_options::binding_input_blocked();
    std::lock_guard lock(controller_mutex_);
    const auto result=window_focused_ && gameplay_context_ && !blocked && !binding_pump_blocked_
            ? pending_weapon_cycle_ : 0;
    pending_weapon_cycle_=0;
    return result;
}

bool RageWarsDesktopRenderer::initialize(std::string &error) {
    return initialize(error,xr_requested());
}

bool RageWarsDesktopRenderer::initialize(std::string &error,bool attempt_xr) {
    if (stats_.initialized) return true;
#ifdef XR64_OPENXR
    if(!g_independent_policy_initialized) {
        g_independent_presentation=independent_requested();
        g_independent_policy_initialized=true;
    }
    g_xr_active.store(false,std::memory_order_release);
    g_xr_session_deadline={};
    g_xr_queued_request.store(0,std::memory_order_release);
    g_xr_context_capable=true;
    set_xr_transition_status(XrPresentationMode::Desktop,"Desktop starting");
#endif
    if (!load_sdl(error)) return false;
#ifndef XR64_DEMO_BUILD
    const char* freeze_option = std::getenv("XR64_RW_AI_FREEZE");
    rw092_ai_freeze.store(freeze_option && std::string(freeze_option) == "1",
            std::memory_order_relaxed);
    if (std::getenv("XR64_RW_CAPTURE_DIR") == nullptr) {
        const auto directory = std::filesystem::temp_directory_path() / "XR64-render-captures";
        _putenv_s("XR64_RW_CAPTURE_DIR", directory.string().c_str());
    }
    g_bypass_mesh_shading = ::xr64::render_diagnostics::enabled("XR64_RW_BYPASS_MESH_SHADING");
    std::fprintf(stderr, "RW081_DIAGNOSTICS legacy=%u framebuffer_probes=%u texture_capture=%u capture_dir=%s keys=F1-HUD,F2-size,F3-shade,F9-capture,F11-fullscreen\n",
            ::xr64::render_diagnostics::legacy(), ::xr64::render_diagnostics::readbacks(),
            ::xr64::render_diagnostics::texture_capture(), std::getenv("XR64_RW_CAPTURE_DIR"));
#endif
    if (g_sdl.init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        error = sdl_error();
        return false;
    }
#ifdef XR64_OPENXR
    // Keep a compatibility context capable of later XR attachment even when
    // startup preference resolves to desktop. Fall back to legacy desktop GL
    // below when this context version is unavailable.
    g_sdl.gl_set_attribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    g_sdl.gl_set_attribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    g_sdl.gl_set_attribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#else
    g_sdl.gl_set_attribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    g_sdl.gl_set_attribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
#endif
    SDL_Window *window = g_sdl.create_window("Rage Wars Recompiled", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        error = sdl_error();
        ::xr64::rage_wars::audio::shutdown();
    g_sdl.quit();
        return false;
    }
    SDL_GLContext context = g_sdl.gl_create_context(window);
#ifdef XR64_OPENXR
    if(context==nullptr) {
        // Preserve a usable PC renderer on GPUs/drivers without a 4.3
        // compatibility context; live XR entry will report this capability miss.
        g_xr_context_capable=false;
        g_sdl.gl_set_attribute(SDL_GL_CONTEXT_PROFILE_MASK,0);
        g_sdl.gl_set_attribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
        g_sdl.gl_set_attribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
        context=g_sdl.gl_create_context(window);
    }
#endif
    if (context == nullptr) {
        error = sdl_error();
        g_sdl.destroy_window(window);
        ::xr64::rage_wars::audio::shutdown();
        g_sdl.quit();
        return false;
    }
#ifdef XR64_OPENXR
    ensure_present_swap_interval(attempt_xr ? 0 : 1);
#else
    ensure_present_swap_interval(1);
#endif
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glEnable(GL_DEPTH_TEST);
    g_sdl.gl_swap_window(window);
#ifdef XR64_OPENXR
    // XR is optional. Startup preference is separate from live mode; failed
    // startup initialization leaves this same PC renderer ready for later entry.
    if(attempt_xr) {
        std::string xr_error;
        (void)start_xr_on_owner(xr_error);
        if(!xr_enabled())ensure_present_swap_interval(1);
    } else {
        set_xr_transition_status(XrPresentationMode::Desktop,"Desktop active");
    }
#endif
    g_sdl.gl_make_current(window, nullptr);
    window_ = window;
    context_ = context;
    stats_.initialized = true;
    if (const char *sensitivity = std::getenv("XR64_MOUSE_SENSITIVITY");
            sensitivity != nullptr && sensitivity[0] != '\0') {
        char *end = nullptr;
        const float parsed = std::strtof(sensitivity, &end);
        if (end != sensitivity && *end == '\0' && parsed >= 0.001F && parsed <= 1.0F) {
            mouse_sensitivity_ = parsed;
        }
    }
    {
        std::lock_guard lock(controller_mutex_);
        controller_snapshot_.connected = true;
        controller_snapshot_.device_name = "Keyboard + Mouse";
    }
    set_mouse_capture(true);
    std::fprintf(stderr,
            "RW101_KEYBOARD_MOUSE_READY sensitivity=%.3f controls=WASD/mouse,LMB-fire,Space-jump,R/RMB-action,Q/E-weapons,Enter/Esc-start,Arrows-aim,IJKL-dpad\n",
            mouse_sensitivity_);
    const int device_count = g_sdl.num_joysticks();
    std::fprintf(stderr, "RW077_SDL_ENUMERATE devices=%d\n", device_count);
    for (int index = 0; index < device_count; ++index) {
        const bool mapped = g_sdl.is_game_controller(index) == SDL_TRUE;
        const char *name = mapped ? g_sdl.game_controller_name_for_index(index)
                                  : g_sdl.joystick_name_for_index(index);
        std::fprintf(stderr, "RW077_SDL_DEVICE index=%d name=%s mapped=%s\n",
                index, name != nullptr ? name : "unknown", mapped ? "yes" : "no");
        if (mapped && controller_ == nullptr) open_controller(index);
    }
    XR64_RENDER_DIAGNOSTIC_FLUSH();
    return true;
}

bool RageWarsDesktopRenderer::submit_task(
        std::uint8_t *rdram, std::size_t rdram_size,
        std::uint32_t task_address, std::string &error) {
    // The runtime may stop the graphics owner while the guest still submits
    // its final task. Keep the XR/GL teardown and task entry mutually exclusive.
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (shutting_down_) return true;
    const auto audio_frame_begin = audio::telemetry::now_ns();
    const bool independent = independent_presentation();
    if (!stats_.initialized) {
        if (independent) { error = "Decode before graphics-owner initialization"; return false; }
        if (!initialize(error)) return false;
    }
    SDL_Window *window = static_cast<SDL_Window *>(window_);
    // The active resident guest hook runs on the producer thread. In independent
    // mode this method only records owning CPU commands; GL stays on Gfx Thread.
    if (!independent && g_sdl.gl_make_current(window, static_cast<SDL_GLContext>(context_)) != 0) {
        error = sdl_error();
        return false;
    }
#ifdef XR64_OPENXR
    // Task-coupled mode is owned by this graphics thread at task boundaries.
    // Independent mode applies requests from present_latest instead.
    if(!independent)apply_xr_transition_request_on_owner();
    if(!independent)ensure_present_swap_interval(xr_enabled() ? 0 : 1);
#endif
    const std::uint32_t task_physical = task_address & 0x1FFFFFFFU;
    const OSTask *task = reinterpret_cast<const OSTask *>(rdram + task_physical);
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr,
            "RW018_RENDER_HANDOFF rdram_host=%p rdram_size=%zu task_guest=0x%08X "
            "task_physical=0x%08X task_host=%p type=0x%08X flags=0x%08X "
            "ucode_boot=0x%08X/%u ucode=0x%08X/%u ucode_data=0x%08X/%u "
            "dram_stack=0x%08X/%u output_buff=0x%08X output_buff_size=0x%08X "
            "data_ptr=0x%08X/%u yield_data=0x%08X/%u\n",
            static_cast<void *>(rdram),
            rdram_size, task_address,
            task_physical, static_cast<const void *>(task),
            static_cast<unsigned>(task->t.type), static_cast<unsigned>(task->t.flags),
            static_cast<unsigned>(task->t.ucode_boot),
            static_cast<unsigned>(task->t.ucode_boot_size),
            static_cast<unsigned>(task->t.ucode), static_cast<unsigned>(task->t.ucode_size),
            static_cast<unsigned>(task->t.ucode_data),
            static_cast<unsigned>(task->t.ucode_data_size),
            static_cast<unsigned>(task->t.dram_stack),
            static_cast<unsigned>(task->t.dram_stack_size),
            static_cast<unsigned>(task->t.output_buff),
            static_cast<unsigned>(task->t.output_buff_size),
            static_cast<unsigned>(task->t.data_ptr), static_cast<unsigned>(task->t.data_size),
            static_cast<unsigned>(task->t.yield_data_ptr),
            static_cast<unsigned>(task->t.yield_data_size));
    XR64_RENDER_DIAGNOSTIC_FLUSH();
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr, "RW018_RENDER_STAGE task_fields_verified\n");
    XR64_RENDER_DIAGNOSTIC_FLUSH();
    ::xr64::rage_wars::LiveN64TaskContext live_task;
    { std::lock_guard<std::mutex> lock(stats_mutex_); live_task.sequence = ++stats_.task_count; }
    OpenGlFast3DBackend backend(window, live_task.sequence);
    ::xr64::rage_wars::RageWarsGraphicsBridge bridge(&backend);
    live_task.rsp_task_address = task_address;
    live_task.task_data_address = static_cast<std::uint32_t>(task->t.data_ptr);
    live_task.task_data_size = static_cast<std::uint32_t>(task->t.data_size);
    live_task.microcode_address = static_cast<std::uint32_t>(task->t.ucode);
    live_task.microcode_size = static_cast<std::uint32_t>(task->t.ucode_size);
    live_task.microcode_data_address = static_cast<std::uint32_t>(task->t.ucode_data);
    live_task.microcode_data_size = static_cast<std::uint32_t>(task->t.ucode_data_size);
    live_task.rdram = rdram;
    live_task.rdram_size = rdram_size;
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr,
            "RW018_RENDER_STAGE bridge_submit_begin task_guest=0x%08X "
            "dl_guest=0x%08X dl_physical=0x%08X ucode_guest=0x%08X "
            "ucode_physical=0x%08X ucode_data_guest=0x%08X "
            "ucode_data_physical=0x%08X canonical_rdram=%p canonical_size=%zu\n",
            live_task.rsp_task_address, live_task.task_data_address,
            live_task.task_data_address & 0x1FFFFFFFU, live_task.microcode_address,
            live_task.microcode_address & 0x1FFFFFFFU,
            live_task.microcode_data_address,
            live_task.microcode_data_address & 0x1FFFFFFFU,
            static_cast<const void *>(live_task.rdram), live_task.rdram_size);
    XR64_RENDER_DIAGNOSTIC_FLUSH();
    const auto decode_begin = std::chrono::steady_clock::now();
    const ::xr64::rage_wars::GraphicsBridgeResult result =
            bridge.submit_raw(live_task, backend);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.last_task_address = task_address;
        stats_.last_display_list = live_task.task_data_address;
        stats_.last_triangles = result.triangle_count;
    }
    if (!result.translated) {
        error = result.detail;
        return false;
    }
    if (audio_frame_begin) audio::telemetry::frame(audio::telemetry::guest_context(rdram),
        audio::telemetry::now_ns() - audio_frame_begin, independent);
#ifdef XR64_OPENXR
    if(independent || xr_enabled()) {
        const double decode_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decode_begin).count();
        g_xr_frames.publish({std::move(backend.decoded_frame), live_task.sequence, decode_ms, backend.decoded_stats(), backend.menu_panel(), backend.startup(), std::chrono::steady_clock::now(), backend.scene_player()});
        // Independent scheduling publishes owning commands for desktop and XR;
        // the graphics owner alone replays/presents them and applies transitions.
        return independent ? true : present_latest(error);
    }
#endif
    { std::lock_guard<std::mutex> lock(stats_mutex_); ++stats_.presented_frames; }
    return true;
}

bool RageWarsDesktopRenderer::independent_presentation() const {
#ifdef XR64_OPENXR
    return g_independent_presentation;
#else
    return false;
#endif
}

#include "rage_wars_weapon_calibration_room.inl"

bool RageWarsDesktopRenderer::present_latest(std::string& error) {
#ifdef XR64_OPENXR
    const auto audio_present_begin = audio::telemetry::now_ns();
    const auto audio_present_context = audio::telemetry::context();
    if (!stats_.initialized) { error = "Presentation before desktop initialization"; return false; }
    auto* window = static_cast<SDL_Window*>(window_);
    if (g_sdl.gl_make_current(window, static_cast<SDL_GLContext>(context_)) != 0) {
        error = sdl_error(); return false;
    }
    // This function runs on the sole graphics owner. It is the safe boundary
    // for independent mode; task-coupled requests are applied in submit_task.
    if(independent_presentation())apply_xr_transition_request_on_owner();
    if (!xr_enabled() && (g_sdl.get_window_flags(window) & SDL_WINDOW_MINIMIZED)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        return true;
    }
    auto snapshot = g_xr_frames.acquire();
    const auto present_desktop=[&]() {
        const auto presentation_begin=std::chrono::steady_clock::now();
        refresh_desktop_cadence(window);
        const auto desktop_begin = std::chrono::steady_clock::now();
        static const bool bounded_desktop = [] {
            const char* option = std::getenv("XR64_AUDIO_DESKTOP_PACING");
            return option && std::string(option) == "1";
        }();
        ensure_present_swap_interval(g_desktop_pacing.software?0:1);
        if(!snapshot) {
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            g_sdl.gl_swap_window(window);
            finish_desktop_cadence(presentation_begin);
            return true;
        }
        g_xr_eye=nullptr;
        OpenGlFast3DBackend backend(window,snapshot->generation,snapshot);
        const auto local=local_camera_correction(snapshot->player,local_presentation().acquire(),
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        backend.local_camera(local);
        static std::uint64_t local_frames=0;
        if(++local_frames%120==0 && std::getenv("XR64_LOCAL_PRESENTATION_TRACE"))std::fprintf(stderr,
            "RW_LOCAL_PRESENT generation=%llu input=%llu player=%llu valid=%d yaw=%.7f pitch=%.7f origin=(%.3f,%.3f,%.3f)\n",
            snapshot->generation,local.input_sequence,local.player_update,int(local.valid),local.yaw,local.pitch,
            local.target[12],local.target[13],local.target[14]);
        if(!snapshot->frame.replay(backend,error))return false;
        const bool repeated=snapshot->generation==g_last_presented_generation;
        if(!backend.present_mirror(snapshot->stats,repeated,error))return false;
        if (audio_present_begin) audio::telemetry::presentation(audio_present_context,
            audio::telemetry::now_ns() - audio_present_begin);
        { std::lock_guard<std::mutex> lock(stats_mutex_); ++stats_.presented_frames; }
        g_last_presented_generation=snapshot->generation;
        const auto tick = g_presentation_clock.sample(HostPresentationMode::Desktop,
                snapshot->generation, snapshot->decoded_at);
        if (triangle_perf_enabled()) g_presentation_perf.record(tick);
        finish_desktop_cadence(presentation_begin);
        publish_local_control_state(snapshot->generation,local);
        if(control_sample_enabled()) {
            static std::uint64_t sample_presentations=0;static auto reported=std::chrono::steady_clock::now();
            ++sample_presentations;const auto now=std::chrono::steady_clock::now();
            if(now-reported>=std::chrono::seconds(1)) {
                reported=now;const auto state=local_presentation().acquire();const auto& p=state.player;
                std::fprintf(stderr,"RW_LOCAL_SAMPLE ns=%lld presentations=%llu generation=%llu begin=%llu completed=%llu period_ns=%lld work_ns=%lld age_ns=%lld actor=%08X valid=%d continuity=%llu input=%llu applied=%llu yaw_total=%.9f pitch_total=%.9f yaw=%.9f pitch=%.9f late_yaw=%.9f late_pitch=%.9f up=(%.7f,%.7f,%.7f) origin=(%.5f,%.5f,%.5f) checks=%llu errors=%llu hz=%d\n",
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(),sample_presentations,snapshot->generation,
                    state.update_begins,p.update,p.update_period_ns,p.update_work_ns,
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()-p.sampled_ns,p.actor,int(p.valid),p.continuity,
                    state.mouse.sequence,p.mouse.sequence,state.mouse.yaw,state.mouse.pitch,p.yaw,p.pitch,local.yaw,local.pitch,
                    local.target[4],local.target[5],local.target[6],p.origin[0],p.origin[1],p.origin[2],state.look_checks,state.look_errors,g_desktop_pacing.refresh_hz);
            }
        }
        // Bound repeated independent desktop work when the driver does not pace swaps.
        // This graphics-owner wait does not change guest clocks or SP/AI completion.
        if (bounded_desktop) std::this_thread::sleep_until(
            desktop_begin + std::chrono::nanoseconds(1000000000LL / 60));
        return true;
    };
    if(!xr_enabled()) {
        error.clear();
        return present_desktop();
    }
    if (!g_xr.initialize(error)) {
        const std::string failure="XR initialization failed: "+error;
        detach_xr_to_desktop("VR unavailable: "+failure);
        error.clear();
        return present_desktop();
    }
    ensure_present_swap_interval(0);
    bool first_eye = true;
    XrMotionInput frame_motion;
    LocalCameraCorrection frame_local;
    g_upload_cpu_ms = 0; g_upload_calls = 0;
    XrFrameTiming timing;
    const bool ok = g_xr.render([&](const XrEyeFrame& eye) {
        // Choose the latest completed decode after xrWaitFrame, then pin it for
        // both eyes. Producer publication continues while this frame is rendered.
        if (first_eye) {
            snapshot=g_xr_frames.acquire();first_eye=false;
            frame_motion=gate5_xr_motion_snapshot();
            if(snapshot)frame_local=local_camera_correction(snapshot->player,local_presentation().acquire(),
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        if(!snapshot)return true;
        // Composite a complete flat guest screen before giving it binocular depth.
        // Classification is owned by the decoded task, never sampled from live RAM here.
        auto flat_eye=eye;flat_eye.width=1024;flat_eye.height=768;
        g_xr_menu_flat=snapshot->menu_panel;
        g_xr_eye = g_xr_menu_flat ? &flat_eye : &eye;
        OpenGlFast3DBackend backend(window, snapshot->generation, snapshot);
        backend.local_camera(frame_local);backend.presentation_motion(frame_motion);
        const bool replayed = snapshot->frame.replay(backend, error);
        // The hand ray and flat panel are outside decoded replay ownership.
        g_xr_retained_geometry.restore_draw_scope();
        if(replayed && g_xr_menu_flat) {
            const bool create=g_xr_panel_texture==0;
            if(create)glGenTextures(1,&g_xr_panel_texture);
            glBindTexture(GL_TEXTURE_2D,g_xr_panel_texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            if(create)glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,0,0,1024,768,0);
            else glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,1024,768);
            glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glViewport(0,0,eye.width,eye.height);
            glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);
            glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
            glColor4ub(255,255,255,255);
            glBegin(GL_QUADS);
            for(const auto& c:std::array<std::array<float,2>,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}}) {
                const auto v=xr_menu_corner(c[0],c[1],eye);
                glTexCoord2f((c[0]+1)*0.5F,(c[1]+1)*0.5F);glVertex4f(v.x,v.y,v.z,v.w);
            }
            glEnd();
            static bool reported=false;
            if(!reported){reported=true;std::fprintf(stderr,"RW120_XR_MENU_PANEL distance_m=2 width_m=2 height_m=1.5 source=1024x768\n");}
        } else if(replayed)draw_xr_hand_ray(eye,frame_motion);
        g_xr_eye = nullptr;g_xr_menu_flat=false;
        return replayed;
    }, [&]() {
        // Keep the HUD stable on repeats, but do not recapture the same task.
        OpenGlFast3DBackend mirror(window, snapshot->generation, snapshot);
        return mirror.present_mirror(snapshot->stats,
                snapshot->generation == g_last_presented_generation, error);
    }, error, snapshot != nullptr, &timing);
    g_xr_eye = nullptr;
    if (!ok) {
        const std::string failure=error.empty()?"OpenXR frame/session failed":error;
        detach_xr_to_desktop("VR unavailable: "+failure);
        error.clear();
        return present_desktop();
    }
    const auto xr_mode=g_xr_mode.load(std::memory_order_acquire);
    if(g_xr.session_running()) {
        g_xr_session_deadline={};
        const auto status=g_xr.session_focused()?"VR active; headset focused":"VR active; waiting for headset focus";
        set_xr_transition_status(XrPresentationMode::XRRunning,status);
    } else if(xr_mode==XrPresentationMode::XRRunning) {
        g_xr_session_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        set_xr_transition_status(XrPresentationMode::StartingXR,"Starting VR; session restarting");
    }
    if(g_xr_mode.load(std::memory_order_acquire)==XrPresentationMode::StartingXR &&
            g_xr_session_deadline!=std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now()>=g_xr_session_deadline) {
        detach_xr_to_desktop("VR unavailable: OpenXR session did not become ready within 15 seconds");
        g_xr_session_deadline={};
        error.clear();
        return present_desktop();
    }
    if (!timing.began) {
        // Keep the monitor showing the latest guest frame while the runtime
        // is bringing its session to READY or restarting it after STOPPING.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!g_xr.session_running()) {
            error.clear();
            return present_desktop();
        }
        return true;
    }
    if (timing.rendered) {
        if(snapshot)publish_local_control_state(snapshot->generation,frame_local,frame_motion);
        { std::lock_guard<std::mutex> lock(stats_mutex_); ++stats_.presented_frames; }
        const bool repeated = snapshot->generation == g_last_presented_generation;
        ++g_xr_presentations;
        const char* detail = std::getenv("XR64_XR_TRACE_FRAMES");
        if ((detail && std::string(detail)=="1") || g_xr_presentations%120==0) {
            std::fprintf(stderr, "RW_XR_PRESENT frame=%llu generation=%llu repeated=%d mode=%s commands=%zu decode_cpu_ms=%.3f period_ms=%.3f app_gap_ms=%.3f wait_ms=%.3f acquire_ms=%.3f image_wait_ms=%.3f left_cpu_ms=%.3f right_cpu_ms=%.3f upload_cpu_ms=%.3f uploads=%llu mirror_copy_ms=%.3f mirror_present_ms=%.3f end_ms=%.3f predicted_misses=%llu gpu_span_ms=%.3f gpu_sample_frame=%llu predicted_ns=%lld pose_y=%.6f pose_qy=%.6f pose_qw=%.6f\n",
                    static_cast<unsigned long long>(g_xr_presentations), static_cast<unsigned long long>(snapshot->generation), repeated,
                    independent_presentation()?"independent":"task", snapshot->frame.size(), snapshot->decode_cpu_ms,
                    timing.period_ms, timing.app_gap_ms, timing.wait_ms, timing.acquire_ms, timing.image_wait_ms,
                    timing.eye_ms[0], timing.eye_ms[1], g_upload_cpu_ms, static_cast<unsigned long long>(g_upload_calls),
                    timing.mirror_copy_ms, timing.mirror_present_ms, timing.end_ms, static_cast<unsigned long long>(timing.predicted_misses), timing.gpu_span_ms, timing.gpu_sample_frame, timing.predicted_ns, timing.pose_y, timing.pose_qy, timing.pose_qw);
        }
        g_last_presented_generation = snapshot->generation;
        const auto tick = g_presentation_clock.sample(HostPresentationMode::OpenXR,
                snapshot->generation, snapshot->decoded_at);
        if (triangle_perf_enabled()) g_presentation_perf.record(tick);
    }
#endif
    return true;
}

void RageWarsDesktopRenderer::shutdown() {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (!stats_.initialized || shutting_down_) return;
    shutting_down_ = true;
    g_presentation_clock.reset();
    g_presentation_perf = {};
#ifdef XR64_OPENXR
    g_xr_frames.reset(); g_last_presented_generation = 0; g_xr_presentations = 0;
    g_xr_active.store(false,std::memory_order_release);
    g_xr_queued_request.store(0,std::memory_order_release);
    set_xr_transition_status(XrPresentationMode::StoppingXR,"Stopping renderer");
#endif
    close_controller();
    if (context_) {
        g_sdl.gl_make_current(static_cast<SDL_Window*>(window_),
                static_cast<SDL_GLContext>(context_));
        if (g_hud_font) { glDeleteLists(g_hud_font, 96); g_hud_font = 0; }
#ifdef XR64_OPENXR
        g_xr.shutdown();
        if (g_xr_panel_texture) { glDeleteTextures(1, &g_xr_panel_texture); g_xr_panel_texture = 0; }
#endif
#ifdef XR64_OPENXR
        g_xr_retained_geometry.release_current_context();
#endif
        g_triangle_vbo.release_current_context();
        g_texture_cache.clear();
    }
    g_sdl.gl_delete_context(static_cast<SDL_GLContext>(context_));
    g_present_swap_interval = -1;
    g_desktop_pacing={};g_display_checked={};
    g_sdl.destroy_window(static_cast<SDL_Window *>(window_));
    ::xr64::rage_wars::audio::shutdown();
    g_sdl.quit();
#ifdef XR64_OPENXR
    set_xr_transition_status(XrPresentationMode::Desktop,"Renderer stopped");
#endif
    window_ = nullptr;
    context_ = nullptr;
    stats_.initialized = false;
}

} // namespace xr64::rage_wars::recomp
