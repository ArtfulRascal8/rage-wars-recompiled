// XR64 PC prototype. OpenXR lifecycle follows the public Khronos API contract.
// Perfect Dark VR was consulted for lifecycle and tracking-space separation;
// this implementation does not import its game-specific camera/input code.
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <Windows.h>
#include <GL/gl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "rage_wars_openxr.hpp"
#include "rage_wars_xr_calibration.hpp"
#include "rage_wars_xr_color.hpp"
#include <array>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <cstdlib>
namespace xr64::rage_wars::recomp {
namespace {
XrInstance instance=XR_NULL_HANDLE;
XrSession session=XR_NULL_HANDLE;
XrSpace space=XR_NULL_HANDLE, view_space=XR_NULL_HANDLE;
PFN_xrConvertWin32PerformanceCounterToTimeKHR counter_to_time=nullptr;
XrReferenceEpoch reference_epoch;
XrSessionState session_state=XR_SESSION_STATE_UNKNOWN;
XrActionSet touch_set=XR_NULL_HANDLE;
XrPath left_hand_path=XR_NULL_PATH,right_hand_path=XR_NULL_PATH;
XrAction left_pose=XR_NULL_HANDLE, right_pose=XR_NULL_HANDLE, right_aim_pose=XR_NULL_HANDLE;
XrAction left_stick=XR_NULL_HANDLE, right_stick=XR_NULL_HANDLE, right_stick_click=XR_NULL_HANDLE;
XrAction right_trigger=XR_NULL_HANDLE, right_grip=XR_NULL_HANDLE;
XrAction left_menu=XR_NULL_HANDLE, left_x=XR_NULL_HANDLE, left_y=XR_NULL_HANDLE;
XrAction right_a=XR_NULL_HANDLE, right_b=XR_NULL_HANDLE;
XrSpace left_action_space=XR_NULL_HANDLE, right_action_space=XR_NULL_HANDLE, right_aim_action_space=XR_NULL_HANDLE;
bool touch_ready=false;
bool right_stick_click_was_down=false;
std::mutex input_mutex;
XrMotionInput input_state, shot_state;
unsigned long long input_sequence=0;
void publish_input(XrMotionInput next,XrMotionInput shot={}) {
    std::lock_guard<std::mutex> lock(input_mutex);
    if(!next.host_monotonic_ns)next.host_monotonic_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    next.sequence=++input_sequence;next.space_epoch=reference_epoch.epoch;
    shot.sequence=next.sequence;shot.space_epoch=reference_epoch.epoch;
    input_state=next;shot_state=shot;
}

XrSystemId system=XR_NULL_SYSTEM_ID;
bool running=false;
bool exiting=false;
bool calibrated=false;
using Clock = std::chrono::steady_clock;
Clock::time_point last_end{};
XrTime last_prediction=0;
bool have_last_prediction=false;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
std::atomic<bool> recenter{false};
XrVector3f origin{};
XrQuaternionf originQ{0,0,0,1};
XrVector3f rotate(XrQuaternionf q,XrVector3f v) {
    XrVector3f t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
XrQuaternionf multiply(XrQuaternionf a,XrQuaternionf b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
struct Eye {
    XrSwapchain chain=XR_NULL_HANDLE;
    int width=0,height=0;
    std::vector<XrSwapchainImageOpenGLKHR> images;
    GLuint fbo=0,depth=0;
};
std::array<Eye,2> eyes;
xr_color::Selection color_selection;
bool framebuffer_srgb_supported=false;
std::array<bool,2> color_eye_reported{};
using Blit = void(APIENTRY*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
Blit blit=nullptr;
using Gen = void(APIENTRY*)(GLsizei,GLuint*);
using Bind = void(APIENTRY*)(GLenum,GLuint);
using Delete = void(APIENTRY*)(GLsizei,const GLuint*);
using Attach = void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint);
using Storage = void(APIENTRY*)(GLenum,GLenum,GLsizei,GLsizei);
using AttachDepth = void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint);
using Status = GLenum(APIENTRY*)(GLenum);
using AttachmentParameter = void(APIENTRY*)(GLenum,GLenum,GLenum,GLint*);
AttachmentParameter attachmentParameter=nullptr;
Gen genFbo=nullptr,genRb=nullptr;
Bind bindFbo=nullptr,bindRb=nullptr;
Delete deleteFbo=nullptr,deleteRb=nullptr;
Attach attach=nullptr;
Storage storage=nullptr;
AttachDepth attachDepth=nullptr;
Status status=nullptr;
using QueryCounter = void(APIENTRY*)(GLuint, GLenum);
using QueryAvailable = void(APIENTRY*)(GLuint, GLenum, GLint*);
using QueryResult = void(APIENTRY*)(GLuint, GLenum, unsigned long long*);
Gen genQueries=nullptr;
Delete deleteQueries=nullptr;
QueryCounter queryCounter=nullptr;
QueryAvailable queryAvailable=nullptr;
QueryResult queryResult=nullptr;
struct GpuSample { GLuint query[2]={}; bool pending=false; unsigned long long frame=0; };
std::array<GpuSample,8> gpu_samples;
unsigned long long frame_number=0;
constexpr GLenum FBO=0x8D40,RBO=0x8D41,COLOR=0x8CE0,DEPTH=0x8D00;
bool check(XrResult r,const char* name,std::string& error) {
    if(XR_SUCCEEDED(r)) return true;
    error=std::string(name)+" failed: "+std::to_string(r);
    std::fprintf(stderr,"RW_XR_ERROR %s\n",error.c_str());
    return false;
}
}
bool create_touch_actions(std::string& error) {
    auto path=[&](const char* name,XrPath& out) {
        return check(xrStringToPath(instance,name,&out),name,error);
    };
    XrPath& left=left_hand_path;XrPath& right=right_hand_path;XrPath profile=XR_NULL_PATH;
    if(!path("/user/hand/left",left)||!path("/user/hand/right",right)||
       !path("/interaction_profiles/oculus/touch_controller",profile))return false;
    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(set_info.actionSetName,"rage_wars_touch");
    strcpy_s(set_info.localizedActionSetName,"Rage Wars Touch");
    if(!check(xrCreateActionSet(instance,&set_info,&touch_set),"xrCreateActionSet",error))return false;
    auto action=[&](XrAction& out,const char* name,const char* label,XrActionType type,XrPath hand){
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType=type;info.countSubactionPaths=1;info.subactionPaths=&hand;
        strcpy_s(info.actionName,name);strcpy_s(info.localizedActionName,label);
        return check(xrCreateAction(touch_set,&info,&out),name,error);
    };
    if(!action(left_pose,"left_pose","Left hand",XR_ACTION_TYPE_POSE_INPUT,left)||
       !action(right_pose,"right_pose","Right hand",XR_ACTION_TYPE_POSE_INPUT,right)||
       !action(right_aim_pose,"right_aim_pose","Right aim",XR_ACTION_TYPE_POSE_INPUT,right)||
       !action(left_stick,"left_stick","Move",XR_ACTION_TYPE_VECTOR2F_INPUT,left)||
       !action(right_stick,"right_stick","Turn",XR_ACTION_TYPE_VECTOR2F_INPUT,right)||
       !action(right_stick_click,"right_stick_click","Reset view",XR_ACTION_TYPE_BOOLEAN_INPUT,right)||
       !action(right_trigger,"right_trigger","Fire",XR_ACTION_TYPE_FLOAT_INPUT,right)||
       !action(right_grip,"right_grip","Secondary",XR_ACTION_TYPE_FLOAT_INPUT,right)||
       !action(left_menu,"left_menu","Start",XR_ACTION_TYPE_BOOLEAN_INPUT,left)||
       !action(left_x,"left_x","Previous weapon",XR_ACTION_TYPE_BOOLEAN_INPUT,left)||
       !action(left_y,"left_y","Next weapon",XR_ACTION_TYPE_BOOLEAN_INPUT,left)||
       !action(right_a,"right_a","A",XR_ACTION_TYPE_BOOLEAN_INPUT,right)||
       !action(right_b,"right_b","B",XR_ACTION_TYPE_BOOLEAN_INPUT,right))return false;
    std::vector<XrActionSuggestedBinding> bindings;
    auto bind=[&](XrAction a,const char* name) {
        XrPath p=XR_NULL_PATH;
        if(!path(name,p))return false;
        bindings.push_back({a,p});
        return true;
    };
    if(!bind(left_pose,"/user/hand/left/input/grip/pose")||
       !bind(right_pose,"/user/hand/right/input/grip/pose")||
       !bind(right_aim_pose,"/user/hand/right/input/aim/pose")||
       !bind(left_stick,"/user/hand/left/input/thumbstick")||
       !bind(right_stick,"/user/hand/right/input/thumbstick")||
       !bind(right_stick_click,"/user/hand/right/input/thumbstick/click")||
       !bind(right_trigger,"/user/hand/right/input/trigger/value")||
       !bind(right_grip,"/user/hand/right/input/squeeze/value")||
       !bind(left_menu,"/user/hand/left/input/menu/click")||
       !bind(left_x,"/user/hand/left/input/x/click")||
       !bind(left_y,"/user/hand/left/input/y/click")||
       !bind(right_a,"/user/hand/right/input/a/click")||
       !bind(right_b,"/user/hand/right/input/b/click"))return false;
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile=profile;
    suggested.countSuggestedBindings=static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings=bindings.data();
    if(!check(xrSuggestInteractionProfileBindings(instance,&suggested),
              "xrSuggestInteractionProfileBindings",error))return false;
    auto action_space=[&](XrAction a,XrPath hand,XrSpace& out){
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.action=a;info.subactionPath=hand;info.poseInActionSpace.orientation.w=1;
        return check(xrCreateActionSpace(session,&info,&out),"xrCreateActionSpace",error);
    };
    if(!action_space(left_pose,left,left_action_space)||
       !action_space(right_pose,right,right_action_space)||
       !action_space(right_aim_pose,right,right_aim_action_space))return false;
    XrSessionActionSetsAttachInfo action_sets{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    action_sets.countActionSets=1;action_sets.actionSets=&touch_set;
    if(!check(xrAttachSessionActionSets(session,&action_sets),"xrAttachSessionActionSets",error))return false;
    touch_ready=true;
    std::fprintf(stderr,"RW_XR_TOUCH actions_attached=1 profile=oculus_touch\n");
    return true;
}
void sample_touch(XrTime predicted,const XrPosef* head_pose) {
    XrMotionInput next;
    next.available=touch_ready;
    next.focused=touch_ready && session_state==XR_SESSION_STATE_FOCUSED;
    next.predicted_display_time.value=static_cast<long long>(predicted);
    next.predicted_ns=next.predicted_display_time.value;
    next.pose_target_time=predicted;next.pose_time_policy=XrPoseTimePolicy::DisplayPrediction;
    if(!next.focused) {publish_input(next);return;}
    XrActiveActionSet active{touch_set,XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets=1;sync.activeActionSets=&active;
    const XrResult sync_result=xrSyncActions(session,&sync);
    if(XR_FAILED(sync_result)){
        std::fprintf(stderr,"RW_XR_TOUCH sync_failed=%d\n",sync_result);
        next.focused=false;publish_input(next);return;
    }
    auto vec=[&](XrAction action,XrMotionHand& hand){
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;
        info.subactionPath=(action==left_stick||action==left_menu||action==left_x||action==left_y||action==left_pose) ? left_hand_path : right_hand_path;
        XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
        if(XR_SUCCEEDED(xrGetActionStateVector2f(session,&info,&state))&&state.isActive){
            hand.stick[0]=state.currentState.x;hand.stick[1]=state.currentState.y;
        }
    };
    auto boolean=[&](XrAction action,bool& out){
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;
        info.subactionPath=(action==left_stick||action==left_menu||action==left_x||action==left_y||action==left_pose) ? left_hand_path : right_hand_path;
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        if(XR_SUCCEEDED(xrGetActionStateBoolean(session,&info,&state))&&state.isActive)
            out=state.currentState==XR_TRUE;
    };
    auto pose=[&](XrAction action,XrSpace action_space,XrMotionHand& hand,bool aim,XrTime target){
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;
        info.subactionPath=(action==left_stick||action==left_menu||action==left_x||action==left_y||action==left_pose) ? left_hand_path : right_hand_path;
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        if(XR_FAILED(xrGetActionStatePose(session,&info,&state))||!state.isActive)return;
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if(XR_FAILED(xrLocateSpace(action_space,space,target,&loc)))return;
        constexpr auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if((loc.locationFlags&valid)!=valid)return;
        XrQuaternionf inverse{-originQ.x,-originQ.y,-originQ.z,originQ.w};
        const auto q=multiply(inverse,loc.pose.orientation);
        const auto p=rotate(inverse,{loc.pose.position.x-origin.x,loc.pose.position.y-origin.y,loc.pose.position.z-origin.z});
        float* position=aim?hand.aim_position:hand.position;
        float* orientation=aim?hand.aim_orientation:hand.orientation;
        position[0]=p.x;position[1]=p.y;position[2]=p.z;
        orientation[0]=q.x;orientation[1]=q.y;orientation[2]=q.z;orientation[3]=q.w;
        if(aim)hand.aim_pose_valid=true;else hand.pose_valid=true;
    };
    vec(left_stick,next.left);vec(right_stick,next.right);
    XrActionStateGetInfo trigger_info{XR_TYPE_ACTION_STATE_GET_INFO};trigger_info.action=right_trigger;trigger_info.subactionPath=right_hand_path;
    XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
    if(XR_SUCCEEDED(xrGetActionStateFloat(session,&trigger_info,&trigger))&&trigger.isActive)
        next.right.trigger=trigger.currentState;
    XrActionStateGetInfo grip_info{XR_TYPE_ACTION_STATE_GET_INFO};grip_info.action=right_grip;grip_info.subactionPath=right_hand_path;
    XrActionStateFloat grip{XR_TYPE_ACTION_STATE_FLOAT};
    if(XR_SUCCEEDED(xrGetActionStateFloat(session,&grip_info,&grip))&&grip.isActive)
        next.right.grip=grip.currentState;
    boolean(left_menu,next.menu_button);
    bool right_stick_click_down=false;
    boolean(right_stick_click,right_stick_click_down);
    if(right_stick_click_down && !right_stick_click_was_down){
        recenter.store(true);
        std::fprintf(stderr,"RW_XR_RECENTER source=right_stick_click requested=1\n");
    }
    right_stick_click_was_down=right_stick_click_down;
    boolean(left_x,next.left.primary);boolean(left_y,next.left.secondary);
    boolean(right_a,next.right.primary);boolean(right_b,next.right.secondary);
    auto head=[&](XrMotionInput& packet,XrTime target,const XrPosef* fallback){
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        const XrPosef* source=fallback;
        if(view_space && XR_SUCCEEDED(xrLocateSpace(view_space,space,target,&loc)) &&
            (loc.locationFlags&valid)==valid)source=&loc.pose;
        if(!calibrated || !source)return;
        XrQuaternionf inverse{-originQ.x,-originQ.y,-originQ.z,originQ.w};
        const auto q=multiply(inverse,source->orientation);
        const auto p=rotate(inverse,{source->position.x-origin.x,source->position.y-origin.y,source->position.z-origin.z});
        packet.head_position[0]=p.x;packet.head_position[1]=p.y;packet.head_position[2]=p.z;
        packet.head_orientation[0]=q.x;packet.head_orientation[1]=q.y;
        packet.head_orientation[2]=q.z;packet.head_orientation[3]=q.w;
        packet.head_pose_valid=true;
    };
    auto poses=[&](XrMotionInput& packet,XrTime target,const XrPosef* fallback){
        head(packet,target,fallback);
        if(calibrated){
            pose(left_pose,left_action_space,packet.left,false,target);
            pose(right_pose,right_action_space,packet.right,false,target);
            pose(right_aim_pose,right_aim_action_space,packet.right,true,target);
        }
    };
    poses(next,predicted,head_pose);
    XrMotionInput shot=next;
    shot.head_pose_valid=shot.left.pose_valid=shot.right.pose_valid=shot.right.aim_pose_valid=false;
    shot.predicted_ns=0;shot.predicted_display_time={};shot.pose_target_time=0;
    shot.pose_time_policy=XrPoseTimePolicy::Unavailable;
    LARGE_INTEGER counter{};XrTime current=0;
    shot.host_monotonic_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    if(counter_to_time && QueryPerformanceCounter(&counter) &&
        XR_SUCCEEDED(counter_to_time(instance,&counter,&current)) && current>0 &&
        current>=reference_epoch.not_before) {
        shot.pose_target_time=current;shot.pose_time_policy=XrPoseTimePolicy::CurrentSample;
        poses(shot,current,nullptr);
    }
    if(frame_number%120==0)
        std::fprintf(stderr,"RW_XR_TOUCH_SAMPLE seq=%llu focused=%d poses=%d,%d aim=%d left=(%.3f,%.3f) right=(%.3f,%.3f) trigger=%.3f grip=%.3f buttons=%d%d%d%d\n",
            input_sequence+1,next.focused,next.left.pose_valid,next.right.pose_valid,next.right.aim_pose_valid,
            next.left.stick[0],next.left.stick[1],next.right.stick[0],next.right.stick[1],
            next.right.trigger,next.right.grip,next.left.primary,next.left.secondary,
            next.right.primary,next.right.secondary);
    publish_input(next,shot);
}
bool RageWarsOpenXr::initialize(std::string& error) {
    if(instance) return true;
    std::vector<const char*> extensions{XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    uint32_t extension_count=0;
    if(!check(xrEnumerateInstanceExtensionProperties(nullptr,0,&extension_count,nullptr),"extension count",error))return false;
    std::vector<XrExtensionProperties> supported(extension_count,{XR_TYPE_EXTENSION_PROPERTIES});
    if(!check(xrEnumerateInstanceExtensionProperties(nullptr,extension_count,&extension_count,supported.data()),"extensions",error))return false;
    for(const auto& extension:supported)
        if(std::strcmp(extension.extensionName,XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME)==0)
            extensions.push_back(XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME);
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName,"XR64 Rage Wars prototype");
    ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    ci.enabledExtensionCount=static_cast<uint32_t>(extensions.size());ci.enabledExtensionNames=extensions.data();
    if(!check(xrCreateInstance(&ci,&instance),"xrCreateInstance",error)) return false;
    if(extensions.size()>1)
        xrGetInstanceProcAddr(instance,"xrConvertWin32PerformanceCounterToTimeKHR",reinterpret_cast<PFN_xrVoidFunction*>(&counter_to_time));
    std::fprintf(stderr,"RW_XR_SHOT_TIME policy=current_sample available=%d max_age_ms=50\n",counter_to_time!=nullptr);
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance,&props);
    std::fprintf(stderr,"RW_XR_RUNTIME name=%s\n",props.runtimeName);
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO}; si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if(!check(xrGetSystem(instance,&si,&system),"xrGetSystem",error)) return false;
    PFN_xrGetOpenGLGraphicsRequirementsKHR requirements=nullptr;
    if(!check(xrGetInstanceProcAddr(instance,"xrGetOpenGLGraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements)),"graphics requirements entry",error)) return false;
    XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if(!check(requirements(instance,system,&req),"graphics requirements",error)) return false;
    int major=0,minor=0; sscanf_s(reinterpret_cast<const char*>(glGetString(GL_VERSION)),"%d.%d",&major,&minor);
    if(XR_MAKE_VERSION(major,minor,0)<req.minApiVersionSupported) { error="OpenGL context below runtime minimum"; return false; }
    XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    binding.hDC=wglGetCurrentDC(); binding.hGLRC=wglGetCurrentContext();
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO}; sci.next=&binding;sci.systemId=system;
    if(!check(xrCreateSession(instance,&sci,&session),"xrCreateSession",error)) return false;
    XrReferenceSpaceCreateInfo ri{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;ri.poseInReferenceSpace.orientation.w=1;
    if(!check(xrCreateReferenceSpace(session,&ri,&space),"xrCreateReferenceSpace",error)) return false;
    ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    if(!check(xrCreateReferenceSpace(session,&ri,&view_space),"view reference space",error))return false;
    // Consumer builds use the tracked Touch action set by default. Developers
    // can still opt out explicitly for runtime/input diagnostics.
    const char* touch_env=std::getenv("XR64_XR_TOUCH");
    const bool touch_requested=!(touch_env && (std::strcmp(touch_env,"0")==0 || std::strcmp(touch_env,"off")==0));
    if(touch_requested) {
        std::string touch_error;
        if(!create_touch_actions(touch_error))
            std::fprintf(stderr,"RW_XR_TOUCH unavailable=%s\n",touch_error.c_str());
    } else {
        std::fprintf(stderr,"RW_XR_TOUCH unavailable=disabled_by_environment\n");
    }

#define XR64_LOAD_GL(field,type,name) field=reinterpret_cast<type>(wglGetProcAddress(name)); if(!field){error=name;return false;}
    XR64_LOAD_GL(genFbo,Gen,"glGenFramebuffers") XR64_LOAD_GL(genRb,Gen,"glGenRenderbuffers")
    XR64_LOAD_GL(bindFbo,Bind,"glBindFramebuffer") XR64_LOAD_GL(bindRb,Bind,"glBindRenderbuffer")
    XR64_LOAD_GL(deleteFbo,Delete,"glDeleteFramebuffers") XR64_LOAD_GL(deleteRb,Delete,"glDeleteRenderbuffers")
    XR64_LOAD_GL(attach,Attach,"glFramebufferTexture2D") XR64_LOAD_GL(storage,Storage,"glRenderbufferStorage")
    XR64_LOAD_GL(attachDepth,AttachDepth,"glFramebufferRenderbuffer") XR64_LOAD_GL(status,Status,"glCheckFramebufferStatus")
    XR64_LOAD_GL(blit,Blit,"glBlitFramebuffer")
    attachmentParameter=reinterpret_cast<AttachmentParameter>(wglGetProcAddress("glGetFramebufferAttachmentParameteriv"));
#undef XR64_LOAD_GL
    // Optional, delayed timestamps. Never wait on queries or call glFinish.
    const char* gl_extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    framebuffer_srgb_supported=major>=3 || (gl_extensions &&
        (std::strstr(gl_extensions,"GL_ARB_framebuffer_sRGB") ||
         std::strstr(gl_extensions,"GL_EXT_framebuffer_sRGB")));
    if (major>3 || (major==3 && minor>=3) || (gl_extensions && std::strstr(gl_extensions,"GL_ARB_timer_query"))) {
        genQueries=reinterpret_cast<Gen>(wglGetProcAddress("glGenQueries"));
        deleteQueries=reinterpret_cast<Delete>(wglGetProcAddress("glDeleteQueries"));
        queryCounter=reinterpret_cast<QueryCounter>(wglGetProcAddress("glQueryCounter"));
        queryAvailable=reinterpret_cast<QueryAvailable>(wglGetProcAddress("glGetQueryObjectiv"));
        queryResult=reinterpret_cast<QueryResult>(wglGetProcAddress("glGetQueryObjectui64v"));
        if(genQueries && deleteQueries && queryCounter && queryAvailable && queryResult)
            for(auto& sample:gpu_samples) genQueries(2,sample.query);
    }
    uint32_t n=0;
    if(!check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&n,nullptr),"view count",error)||n!=2){error="Primary stereo requires two views";return false;}
    std::array<XrViewConfigurationView,2> views{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
    if(!check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&n,views.data()),"views",error)) return false;
    if(!check(xrEnumerateSwapchainFormats(session,0,&n,nullptr),"format count",error)) return false;
    std::vector<int64_t> formats(n);
    if(!check(xrEnumerateSwapchainFormats(session,n,&n,formats.data()),"formats",error)) return false;
    formats.resize(n);
    const bool srgb_requested=xr_color::requested(std::getenv("XR64_XR_SRGB_SWAPCHAIN"));
    color_selection=xr_color::select(formats,srgb_requested,framebuffer_srgb_supported);
    color_eye_reported={};
    xr_color::report("RW_XR_COLOR supported_formats=");
    for(const auto format:formats)xr_color::report("%s(0x%llX) ",xr_color::name(format),static_cast<unsigned long long>(format));
    xr_color::report("\nRW_XR_COLOR requested=%s enabled=%d format=%s(0x%llX) fallback=%d framebuffer_srgb_initial=%s\n",
        srgb_requested?"srgb":"rgba8",color_selection.srgb,xr_color::name(color_selection.format),
        static_cast<unsigned long long>(color_selection.format),color_selection.fallback,
        framebuffer_srgb_supported?(glIsEnabled(xr_color::framebuffer_srgb)?"enabled":"disabled"):"unsupported");
    // Ordinary RGBA textures and fixed-function vertex colors retain encoded
    // legacy arithmetic. The opt-in sRGB format tells OpenXR how to sample the
    // stored bytes; EncodedEyeScope prevents a second encode while drawing.
    if(!color_selection.format){error="Runtime has no usable XR color swapchain format";return false;}
    for(int i=0;i<2;++i){
        auto& e=eyes[i];e.width=views[i].recommendedImageRectWidth;e.height=views[i].recommendedImageRectHeight;
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;sc.format=color_selection.format;sc.sampleCount=1;
        sc.width=e.width;sc.height=e.height;sc.faceCount=1;sc.arraySize=1;sc.mipCount=1;
        if(!check(xrCreateSwapchain(session,&sc,&e.chain),"xrCreateSwapchain",error)) return false;
        if(!check(xrEnumerateSwapchainImages(e.chain,0,&n,nullptr),"image count",error)) return false;
        e.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        if(!check(xrEnumerateSwapchainImages(e.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())),"images",error)) return false;
        genFbo(1,&e.fbo);genRb(1,&e.depth);bindRb(RBO,e.depth);storage(RBO,0x81A6,e.width,e.height);
        std::fprintf(stderr,"RW_XR_EYE index=%d extent=%dx%d\n",i,e.width,e.height);
    }
    return true;
}
bool RageWarsOpenXr::render(const std::function<bool(const XrEyeFrame&)>& draw,const std::function<bool()>& present_mirror,std::string& error, bool have_frame, XrFrameTiming* output) {
    XrFrameTiming local;
    auto& timing = output ? *output : local;
    timing = {};
    if (last_end != Clock::time_point{}) timing.app_gap_ms = elapsed(last_end);
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult polled;
    while((polled=xrPollEvent(instance,&event))==XR_SUCCESS){
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
            const auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
            std::fprintf(stderr,"RW_XR_STATE state=%d\n",state);
            session_state=state;
            if(touch_ready && state!=XR_SESSION_STATE_FOCUSED) {
                XrMotionInput neutral;neutral.available=true;
                publish_input(neutral);
            }
            if(state==XR_SESSION_STATE_READY){XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if(!check(xrBeginSession(session,&bi),"xrBeginSession",error)) return false; running=true;}
            if(state==XR_SESSION_STATE_STOPPING){running=false;if(!check(xrEndSession(session),"xrEndSession",error))return false;}
            if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)exiting=true;
        }
        if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)exiting=true;
        if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const auto& change=*reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
            if(change.session==session && change.referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL)
                reference_epoch.schedule(change.changeTime);
        }
        event={XR_TYPE_EVENT_DATA_BUFFER};
    }
    if(XR_FAILED(polled))return check(polled,"xrPollEvent",error);
    if(exiting){error="OpenXR session exited";return false;}
    if(!running){ if(touch_ready){XrMotionInput neutral;neutral.available=true;publish_input(neutral);} return true; }
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};XrFrameState fs{XR_TYPE_FRAME_STATE};
    const auto wait_start = Clock::now();
    if(!check(xrWaitFrame(session,&wi,&fs),"xrWaitFrame",error))return false;
    timing.wait_ms = elapsed(wait_start);
    timing.period_ms = double(fs.predictedDisplayPeriod)/1000000.0;
    if (have_last_prediction && fs.predictedDisplayPeriod > 0) {
        const auto intervals = (fs.predictedDisplayTime-last_prediction + fs.predictedDisplayPeriod/2)/fs.predictedDisplayPeriod;
        if (intervals>1) timing.predicted_misses = static_cast<unsigned long long>(intervals-1);
    }
    last_prediction=fs.predictedDisplayTime;
    have_last_prediction=true;
    timing.predicted_display_time.value=static_cast<long long>(fs.predictedDisplayTime);
    timing.predicted_ns=timing.predicted_display_time.value;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if(!check(xrBeginFrame(session,&bi),"xrBeginFrame",error))return false;
    timing.began=true;
    if(reference_epoch.advance(fs.predictedDisplayTime,recenter.exchange(false))) {
        calibrated=false;
        publish_input({}); // No old-space shot packet survives a reset.
    }
    ++frame_number;
    GpuSample* gpu_sample=nullptr;
    for(auto& sample:gpu_samples) {
        if(sample.pending) {
            GLint ready=0; queryAvailable(sample.query[1],0x8867,&ready);
            if(ready) {
                unsigned long long start=0,finish=0;
                queryResult(sample.query[0],0x8866,&start);queryResult(sample.query[1],0x8866,&finish);
                if(sample.frame>=timing.gpu_sample_frame) {
                    timing.gpu_span_ms=double(finish-start)/1000000.0;
                    timing.gpu_sample_frame=sample.frame;
                }
                sample.pending=false;
            }
        }
        if(!sample.pending && sample.query[0] && !gpu_sample) gpu_sample=&sample;
    }
    bool gpu_started=false;
    bool mirror_ready=false;
    std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=fs.predictedDisplayTime;li.space=space;
    XrViewState vs{XR_TYPE_VIEW_STATE};uint32_t n=0;
    bool ok=true, rendered=false, sampled_touch=false;
    std::array<XrCompositionLayerProjectionView,2> pv{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
    std::array<uint32_t,2> image_index{};
    std::array<bool,2> image_waited{};
    if(fs.shouldRender && have_frame){
        for(int i=0;i<2 && ok;++i) {
            const auto acquire_start=Clock::now();
            XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            ok=check(xrAcquireSwapchainImage(eyes[i].chain,&ai,&image_index[i]),"acquire",error);
            timing.acquire_ms+=elapsed(acquire_start);if(!ok)break;
            const auto image_wait_start=Clock::now();
            XrSwapchainImageWaitInfo sw{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};sw.timeout=XR_INFINITE_DURATION;
            ok=check(xrWaitSwapchainImage(eyes[i].chain,&sw),"wait image",error);
            timing.image_wait_ms+=elapsed(image_wait_start);image_waited[i]=ok;
        }
        if(ok)ok=check(xrLocateViews(session,&li,&vs,2,&n,views.data()),"xrLocateViews",error);
        const auto flags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        if(ok&&n==2&&(vs.viewStateFlags&flags)==flags){
            if(!calibrated) {
                origin={(views[0].pose.position.x+views[1].pose.position.x)*0.5F,(views[0].pose.position.y+views[1].pose.position.y)*0.5F,(views[0].pose.position.z+views[1].pose.position.z)*0.5F};
                const auto& head=views[0].pose.orientation;
                const auto level=xr_level_yaw_origin({head.x,head.y,head.z,head.w});
                originQ={level[0],level[1],level[2],level[3]};calibrated=true;
                std::fprintf(stderr,"RW_XR_CALIBRATED origin=(%f,%f,%f) level_yaw_q=(%f,%f) provisional_units_per_metre=100\n",origin.x,origin.y,origin.z,originQ.y,originQ.w);
            }
            XrPosef head_pose=views[0].pose;
            head_pose.position={(views[0].pose.position.x+views[1].pose.position.x)*0.5F,
                (views[0].pose.position.y+views[1].pose.position.y)*0.5F,
                (views[0].pose.position.z+views[1].pose.position.z)*0.5F};
            if(touch_ready){sample_touch(fs.predictedDisplayTime,&head_pose);sampled_touch=true;}
            rendered=true;
            if(gpu_sample) { queryCounter(gpu_sample->query[0],0x8E28); gpu_started=true; }
            for(int i=0;i<2&&ok;++i){
                auto& e=eyes[i];const auto index=image_index[i];
                bindFbo(FBO,e.fbo);attach(FBO,COLOR,GL_TEXTURE_2D,e.images[index].image,0);attachDepth(FBO,DEPTH,RBO,e.depth);
                if(status(FBO)!=0x8CD5){error="Incomplete OpenXR framebuffer";ok=false;}
                if(ok){
                    xr_color::EncodedEyeScope color_scope(color_selection.srgb);
                    const bool report_color=!color_eye_reported[i];
                    if(report_color) {
                        GLint encoding=0;
                        if(attachmentParameter && framebuffer_srgb_supported)
                            attachmentParameter(FBO,COLOR,xr_color::attachment_color_encoding,&encoding);
                        xr_color::report("RW_XR_COLOR eye=%d phase=before_draw framebuffer_srgb=%s attachment_encoding=%s(0x%X)\n",
                            i,framebuffer_srgb_supported?(glIsEnabled(xr_color::framebuffer_srgb)?"enabled":"disabled"):"unsupported",
                            encoding==xr_color::srgb_encoding?"sRGB":encoding==GL_LINEAR?"linear":"unknown",encoding);
                    }
                    const auto& v=views[i];XrEyeFrame f;f.width=e.width;f.height=e.height;
                    XrQuaternionf inverse{-originQ.x,-originQ.y,-originQ.z,originQ.w};
                    const auto q=multiply(inverse,v.pose.orientation);
                    const auto pos=rotate(inverse,{v.pose.position.x-origin.x,v.pose.position.y-origin.y,v.pose.position.z-origin.z});
                    f.orientation[0]=q.x;f.orientation[1]=q.y;f.orientation[2]=q.z;f.orientation[3]=q.w;
                    f.position[0]=pos.x;f.position[1]=pos.y;f.position[2]=pos.z;
                    f.fov[0]=v.fov.angleLeft;f.fov[1]=v.fov.angleRight;f.fov[2]=v.fov.angleUp;f.fov[3]=v.fov.angleDown;
                    if(i==0) { timing.pose_y=pos.y; timing.pose_qy=q.y; timing.pose_qw=q.w; }
                    const auto eye_start=Clock::now();
                    ok=draw(f);
                    timing.eye_ms[i]=elapsed(eye_start);
                    if(report_color) {
                        xr_color::report("RW_XR_COLOR eye=%d phase=after_draw framebuffer_srgb=%s draw_ok=%d\n",
                            i,framebuffer_srgb_supported?(glIsEnabled(xr_color::framebuffer_srgb)?"enabled":"disabled"):"unsupported",ok);
                        color_eye_reported[i]=true;
                    }
                    if(ok && i==0) {
                        const auto copy_start=Clock::now();
                        // Copy while the runtime image is still acquired. No third scene draw.
                        RECT client{}; GetClientRect(WindowFromDC(wglGetCurrentDC()), &client);
                        const int w=client.right, h=client.bottom;
                        if(w>0 && h>0) {
                            glDisable(GL_SCISSOR_TEST);
                            bindFbo(0x8CA8,e.fbo); bindFbo(0x8CA9,0);
                            glReadBuffer(COLOR); glDrawBuffer(GL_BACK);
                            glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
                            const float scale=(std::min)(float(w)/e.width,float(h)/e.height);
                            const int mw=int(e.width*scale),mh=int(e.height*scale);
                            blit(0,0,e.width,e.height,(w-mw)/2,(h-mh)/2,(w+mw)/2,(h+mh)/2,GL_COLOR_BUFFER_BIT,GL_NEAREST);
                            bindFbo(FBO,0);
                            mirror_ready=true;
                        }
                        timing.mirror_copy_ms+=elapsed(copy_start);
                    }
                }
                glFlush();bindFbo(FBO,0);
                pv[i].pose=views[i].pose;pv[i].fov=views[i].fov;pv[i].subImage.swapchain=e.chain;pv[i].subImage.imageRect.extent={e.width,e.height};
            }
        }
    }
    // Release every successfully waited image even if locating/drawing failed.
    // A fatal wait failure leaves its acquired image to session teardown.
    for(int i=0;i<2;++i)if(image_waited[i]) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const bool released=check(xrReleaseSwapchainImage(eyes[i].chain,&release),"release",error);
        ok=ok&&released;
    }
    if(touch_ready && !sampled_touch)sample_touch(fs.predictedDisplayTime,nullptr);
    if(gpu_started) {
        queryCounter(gpu_sample->query[1],0x8E28);
        gpu_sample->pending=true; gpu_sample->frame=frame_number;
        glFlush();
    }
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;layer.viewCount=2;layer.views=pv.data();
    const auto* header=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=fs.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount=ok&&rendered?1:0;end.layers=end.layerCount?&header:nullptr;
    const auto end_start=Clock::now();
    const bool ended=check(xrEndFrame(session,&end),"xrEndFrame",error);
    timing.end_ms=elapsed(end_start);
    timing.rendered=ok&&rendered&&ended;
    // The desktop buffer owns the left-eye copy. Present only after both eyes
    // have been released and the XR frame submitted, never between eye draws.
    if (timing.rendered && mirror_ready) {
        const auto mirror_start=Clock::now();
        ok=present_mirror();
        timing.mirror_present_ms=elapsed(mirror_start);
    }
    last_end=Clock::now();
    return ok&&ended;
}
bool RageWarsOpenXr::touch_actions_ready() const { return touch_ready; }
bool RageWarsOpenXr::session_running() const { return running; }
bool RageWarsOpenXr::session_focused() const { return session_state==XR_SESSION_STATE_FOCUSED; }
XrMotionInput RageWarsOpenXr::input_snapshot() const {
    std::lock_guard<std::mutex> lock(input_mutex);
    return input_state;
}
XrMotionInput RageWarsOpenXr::shot_snapshot() const {
    std::lock_guard<std::mutex> lock(input_mutex);
    return shot_state;
}
void RageWarsOpenXr::request_recenter(){recenter.store(true);}
void RageWarsOpenXr::shutdown(){
    last_end={}; last_prediction=0; have_last_prediction=false; frame_number=0;
    for(auto& sample:gpu_samples) {
        if(sample.query[0] && deleteQueries) deleteQueries(2,sample.query);
        sample={};
    }
    for(auto& e:eyes){if(e.fbo&&deleteFbo)deleteFbo(1,&e.fbo);if(e.depth&&deleteRb)deleteRb(1,&e.depth);if(e.chain)xrDestroySwapchain(e.chain);e={};}
    publish_input({});
    if(left_action_space)xrDestroySpace(left_action_space);left_action_space=XR_NULL_HANDLE;
    if(right_action_space)xrDestroySpace(right_action_space);right_action_space=XR_NULL_HANDLE;
    if(right_aim_action_space)xrDestroySpace(right_aim_action_space);right_aim_action_space=XR_NULL_HANDLE;
    if(touch_set)xrDestroyActionSet(touch_set);touch_set=XR_NULL_HANDLE;
    left_pose=right_pose=right_aim_pose=left_stick=right_stick=right_stick_click=right_trigger=right_grip=XR_NULL_HANDLE;
    left_menu=left_x=left_y=right_a=right_b=XR_NULL_HANDLE;
    touch_ready=false;right_stick_click_was_down=false;session_state=XR_SESSION_STATE_UNKNOWN;left_hand_path=right_hand_path=XR_NULL_PATH;
    if(view_space)xrDestroySpace(view_space);view_space=XR_NULL_HANDLE;
    counter_to_time=nullptr;reference_epoch.clear();
    if(space)xrDestroySpace(space);space=XR_NULL_HANDLE;
    if(session)xrDestroySession(session);session=XR_NULL_HANDLE;
    if(instance)xrDestroyInstance(instance);instance=XR_NULL_HANDLE;running=false;exiting=false;calibrated=false;
}
}
