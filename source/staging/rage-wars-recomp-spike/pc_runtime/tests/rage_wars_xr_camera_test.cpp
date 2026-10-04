#include "rage_wars_xr_menu.hpp"
#include "rage_wars_presentation_policy.hpp"
#include "rage_wars_xr_camera.hpp"
#include "rage_wars_local_presentation.hpp"
#include "rage_wars_xr_hand_ray.hpp"
#include "rage_wars_xr_calibration.hpp"
#include "rage_wars_weapon_calibration.hpp"
#include "rage_wars_xr_shot.hpp"
#include <cstdio>
#include <cstdlib>
using namespace xr64::rage_wars::recomp;
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"%s\n",why);std::exit(1);}}
void local_presentation_contract() {
    using namespace xr64::rage_wars;
    LocalPresentationMailbox box;
    controls::Settings settings;
    box.mouse_context(true);box.add_mouse(100,-50,settings);
    const auto first=box.take_mouse();
    require(first.mouse_yaw>0 && first.mouse_pitch>0,"mouse works at zero guest dt");
    require(box.take_mouse().mouse_yaw==0,"guest interval consumed once");
    box.applied_mouse(first,false);box.applied_mouse(first,true);
    LocalPlayerPresentation player;player.valid=true;player.actor=0x80200000;
    player.sampled_ns=100;player.origin={10,20,30};player.yaw=float(first.mouse_yaw);player.pitch=0;
    player.projection={2,0,0,0,0,3,0,0,0,0,1,0.5F,-20,-60,-31,-15};
    box.publish(player);const auto scene=box.capture(player);
    box.add_mouse(50,-25,settings);auto current=box.acquire();
    const auto correction=local_camera_correction(scene,current,101);
    require(correction.valid && std::abs(correction.yaw-float(first.mouse_yaw)*0.5F)<1e-5F,
            "only newer mouse interval changes retained scene");
    ::xr64::N64RawFast3DDrawState draw;draw.perspective_projection=true;
    draw.source_projection=scene.projection;draw.perspective_x_norm=2;
    draw.perspective_y_norm=3;draw.perspective_w_norm=0.5F;
    ::xr64::N64RawFast3DVertex vertex;vertex.w=50;vertex.z=49;vertex.s=0.2F;
    const auto a=local_project_vertex(vertex,draw,correction);
    box.add_mouse(-100,0,settings);
    const auto b=local_project_vertex(vertex,draw,local_camera_correction(scene,box.acquire(),102));
    require(std::abs(a.x-b.x)>1 && a.s==b.s,"same scene yields changed camera with preserved UV");
    const auto second=box.take_mouse();box.applied_mouse(second,false);box.applied_mouse(second,true);
    player.yaw+=float(second.mouse_yaw);player.sampled_ns=102;
    box.publish(player);const auto newer=box.capture(player);
    require(std::abs(local_camera_correction(newer,box.acquire(),103).yaw)<1e-5F,
            "new guest scene does not double mouse interval");
    require(std::abs(local_camera_correction(scene,box.acquire(),103).yaw-
        float(second.mouse_yaw))<1e-5F,"older publication cannot overwrite newer input");
    box.mouse_context(false);box.mouse_context(true);
    require(box.take_mouse().mouse_yaw==0,"focus and mode change discard pending input");
    player.origin[0]+=200;box.publish(player);
    require(!local_camera_correction(scene,box.acquire(),104).valid,"teleport rejects retained history");
    player.valid=false;box.publish(player);
    require(!local_camera_correction(newer,box.acquire(),105).valid,"death/pause rejects player correction");
    require(!local_camera_correction(scene,current,300000101).valid,"stale player state is rejected");
    // Actual guest world is Y-up (native 002349A0 yaw quaternion has only Y).
    LocalPlayerPresentation native;native.valid=true;native.actor=0x80200000;
    native.sampled_ns=100;native.yaw=0;native.pitch=0;
    native.projection={2,0,0,0,0,3,0,0,0,0,1,0.5F,0,0,0,0};
    LocalPresentationState turned;turned.player=native;turned.player.yaw=1.570796F;
    const auto yaw_only=local_camera_correction(native,turned,101);
    require(yaw_only.valid && std::abs(yaw_only.target[4])<1e-5F &&
        std::abs(yaw_only.target[5]-1)<1e-5F && std::abs(yaw_only.target[6])<1e-5F,
        "native horizontal mouse turn preserves upright world Y");
    turned.player.yaw=0;turned.mouse_enabled=true;turned.mouse=native.mouse;
    turned.mouse.pitch=20;turned.player.pitch_high=1.0F;turned.player.pitch_low=-1.0F;
    const auto pitch_only=local_camera_correction(native,turned,101);
    require(pitch_only.valid && std::abs(pitch_only.pitch-1.0F)<1e-5F && pitch_only.target[5]>0,
        "pending pitch uses actual guest bounds and cannot flip upside down");
    draw.source_projection=native.projection;draw.camera_attachment=true;
    require(!local_scene_projection(draw,yaw_only),"first-person gun is camera attached, not world retargeted");
    draw.camera_attachment=false;
    require(local_scene_projection(draw,yaw_only),"world geometry remains eligible for fresh look");
    std::puts("PASS responsive look: displacement, sequence, repeated scene, reconciliation, focus, discontinuities, native axes and gun attachment");
}
void fresh_xr_contract() {
    LocalPlayerPresentation scene;scene.valid=true;scene.actor=0x80200000;
    scene.sampled_ns=100;scene.projection={2,0,0,0,0,3,0,0,0,0,1,0.5F,-100,-600,280,150};
    LocalPresentationState current;current.player=scene;
    current.player.origin={20,10,-5};current.player.sampled_ns=200;
    ::xr64::N64RawFast3DDrawState draw;draw.perspective_projection=true;
    draw.source_projection=scene.projection;draw.perspective_x_norm=2;
    draw.perspective_y_norm=3;draw.perspective_w_norm=0.5F;
    ::xr64::N64RawFast3DVertex v;v.x=40;v.y=25;v.w=200;v.s=0.25F;
    for(float yaw:{0.0F,0.4F,3.0F})for(float pitch:{0.0F,-0.2F,0.3F}) {
        current.player.yaw=yaw;current.player.pitch=pitch;
        const auto body=local_camera_correction(scene,current,201);
        require(body.valid,"fresh collision origin is usable independently of scene generation");
        for(float x:{-0.032F,0.032F}) {
            XrEyeFrame eye;eye.position[0]=x;eye.position[1]=0.1F;eye.position[2]=-0.05F;
            eye.orientation[1]=std::sin(0.15F);eye.orientation[3]=std::cos(0.15F);
            eye.fov[0]=-0.8F;eye.fov[1]=0.8F;eye.fov[2]=0.8F;eye.fov[3]=-0.8F;
            const auto cpu=xr_project_vertex(local_project_vertex(v,draw,body),draw,eye,100);
            const auto retained=xr_project_vertex(v,draw,local_xr_eye(eye,body,100),100);
            for(const auto diff:{cpu.x-retained.x,cpu.y-retained.y,cpu.z-retained.z,cpu.w-retained.w})
                require(std::abs(diff)<0.001F,"CPU and retained geometry share current body/head/eye origin");
        }
    }
    XrReferenceEpoch reference;reference.schedule(200);reference.schedule(300);
    require(!reference.advance(199) && reference.epoch==1,"reference event receipt does not reset early");
    require(reference.advance(200) && reference.epoch==2 && reference.not_before==200 && reference.count==1,
        "reference reset takes effect at its target time and keeps a later event");
    require(!reference.advance(250) && reference.advance(300) && reference.epoch==3,
        "each unrelated reference epoch is handled once without blending");
    require(reference.advance(301,true) && reference.epoch==4,"manual recenter advances epoch once");
    XrMotionInput shot;shot.available=shot.focused=shot.head_pose_valid=true;
    shot.right.pose_valid=shot.right.aim_pose_valid=true;shot.host_monotonic_ns=100;
    shot.pose_target_time=1000;shot.space_epoch=reference.epoch;
    shot.pose_time_policy=XrPoseTimePolicy::DisplayPrediction;
    require(!xr_shot_pose_usable(shot,101),"display-predicted pose cannot be authoritative shot state");
    shot.pose_time_policy=XrPoseTimePolicy::CurrentSample;
    require(xr_shot_pose_usable(shot,101),"current-time pose can be captured at guest fire boundary");
    require(!xr_shot_pose_usable(shot,99) && !xr_shot_pose_usable(shot,50'000'101),
        "future host sample and stale shot sample are rejected");
    XrMotionInput motion;motion.available=motion.focused=true;
    motion.right.pose_valid=motion.right.aim_pose_valid=true;
    motion.right.position[0]=0.2F;motion.right.position[1]=-0.2F;
    const std::array<float,3> local{0.02F,0.03F,-0.4F};
    const auto initial=tracked_weapon_attachment(motion).point(local);
    motion.right.position[0]+=0.1F;
    motion.right.aim_orientation[1]=std::sin(0.2F);motion.right.aim_orientation[3]=std::cos(0.2F);
    const auto fresh=tracked_weapon_attachment(motion);
    require(fresh.valid && std::abs(fresh.point(local)[0]-initial[0])>0.04F,
        "retained weapon geometry follows new controller pose without a guest decode");
    WeaponCalibration calibration;calibration.offset={0.02F,-0.01F,0.03F};
    calibration.degrees={10,20,5};calibration.scale=1.1F;
    const std::array<float,4> placement{0.001F,4,5,6};
    const std::array<float,3> mesh{20,30,40};
    const auto calibrated=weapon_mesh_to_tracking(mesh,placement,calibration,{0,0,0,1},{0,0,0});
    const auto attached=fresh.point(calibrated);
    const auto legacy=weapon_mesh_to_tracking(mesh,placement,calibration,fresh.aim,fresh.grip);
    for(int i=0;i<3;++i)require(std::abs(attached[i]-legacy[i])<1e-6F,
        "late attachment applies existing per-weapon calibration exactly once");
    motion.right.aim_pose_valid=false;
    require(!tracked_weapon_attachment(motion).valid,"tracking loss hides retained weapon instead of using an invalid pose");
    motion.right.aim_pose_valid=true;motion.right.position[0]=std::nanf("");
    require(!tracked_weapon_attachment(motion).valid,"nonfinite grip cannot reach presentation");
    std::puts("PASS fresh XR: retained world/weapon transforms, both eye origins, calibration and invalid tracking");
}
int main(){
#ifdef _WIN32
    if(local_state_sample_enabled()) {
        LocalControlTelemetry writer;constexpr char sample[]="{\"schema\":1}";
        writer.publish(sample,sizeof(sample)-1);
        wchar_t name[96]{};swprintf_s(name,L"Local\\XR64_RageWars_LocalControls_%lu",GetCurrentProcessId());
        const auto mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
        require(mapping!=nullptr,"normal-play telemetry mapping available");
        const auto memory=static_cast<const unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
        require(memory!=nullptr,"read-only telemetry view available");
        std::uint64_t sequence=0;std::uint32_t bytes=0,magic=0;
        std::memcpy(&sequence,memory,8);std::memcpy(&bytes,memory+8,4);std::memcpy(&magic,memory+12,4);
        require(sequence && !(sequence&1) && bytes==sizeof(sample)-1 && magic==0x3157434c &&
            std::memcmp(memory+16,sample,bytes)==0,"bounded latest metadata has completed generation and matching ABI");
        writer.publish(sample,sizeof(sample)-1);std::uint64_t newer=0;std::memcpy(&newer,memory,8);
        require(newer==sequence+2,"metadata replacement advances once without a history queue");
        UnmapViewOfFile(memory);CloseHandle(mapping);
    }
#endif
    DesktopPacingPolicy pacing;pacing.refresh(144);
    require(pacing.period_ns()==1'000'000'000LL/144,"desktop fallback follows 144 Hz display");
    for(int i=0;i<7;++i)pacing.frame(1'000'000,true);
    require(!pacing.software,"isolated short swaps do not immediately override vsync");
    pacing.frame(1'000'000,true);require(pacing.software,"driver overriding vsync gets bounded desktop pacing");
    pacing.refresh(120);pacing.frame(pacing.period_ns(),true);
    require(!pacing.software && pacing.period_ns()==1'000'000'000LL/120,"display change restores normal vsync cadence");
    pacing.frame(1,false);require(pacing.software,"rejected vsync uses current display rate immediately");
    local_presentation_contract();
    fresh_xr_contract();
    // The owner's PC profile used 105 degrees. Both output modes must keep
    // native menu geometry unchanged, including at ultrawide aspect ratios.
    for (bool xr : {false, true}) for (int width : {1440, 1920, 2560})
        for (float fov : {0.0F, 40.0F, 75.0F, 105.0F, 110.0F}) {
            const auto menu=desktop_projection_adjustment(true,xr,true,true,
                    true,width,1080,fov,2.0F);
            require(!menu.wide && menu.x_scale==1 && menu.y_scale==1,
                    "menu preview remains in native artwork coordinates");
        }
    const auto gameplay=desktop_projection_adjustment(false,false,true,true,
            true,1920,1080,90.0F,2.0F);
    require(gameplay.wide && std::abs(gameplay.x_scale-0.375F)<1e-5F &&
            std::abs(gameplay.y_scale-0.5F)<1e-5F,
            "desktop gameplay keeps FOV and widescreen correction");
    const auto hud=desktop_projection_adjustment(false,false,false,true,
            true,1920,1080,105.0F,2.0F);
    require(!hud.wide && hud.x_scale==1 && hud.y_scale==1,
            "orthographic HUD ignores camera preferences");
    const auto inset=desktop_projection_adjustment(false,false,true,false,
            true,1920,1080,105.0F,2.0F);
    require(!inset.wide && inset.x_scale==1 && inset.y_scale==1,
            "inset viewport keeps original projection");

    constexpr float half_sqrt=0.707106781F;
    const auto roll_origin=xr_level_yaw_origin({0,0,half_sqrt,half_sqrt});
    require(std::abs(roll_origin[0])<1e-5F&&std::abs(roll_origin[1])<1e-5F&&
            std::abs(roll_origin[2])<1e-5F&&std::abs(roll_origin[3]-1)<1e-5F,
            "recenter strips headset roll");
    const auto yaw_origin=xr_level_yaw_origin({0,half_sqrt,0,half_sqrt});
    require(std::abs(yaw_origin[0])<1e-5F&&std::abs(yaw_origin[1]-half_sqrt)<1e-5F&&
            std::abs(yaw_origin[2])<1e-5F,"recenter preserves heading");
    ::xr64::N64RawFast3DDrawState s;s.perspective_x_norm=2;s.perspective_y_norm=3;s.perspective_w_norm=0.5F;
    ::xr64::N64RawFast3DVertex v;v.x=0;v.y=0;v.w=50;v.s=0.25F;
    XrEyeFrame e;e.fov[0]=-0.785398163F;e.fov[1]=0.785398163F;e.fov[2]=0.785398163F;e.fov[3]=-0.785398163F;
    auto a=xr_project_vertex(v,s,e,100);require(std::abs(a.x)<1e-5F&&std::abs(a.y)<1e-5F&&a.w==100,"forward point");
    require(a.z/a.w>-1&&a.z/a.w<1,"depth inside frustum");require(a.s==v.s,"texture coordinates preserved");
    e.position[0]=-0.032F;auto left=xr_project_vertex(v,s,e,100);e.position[0]=0.032F;auto right=xr_project_vertex(v,s,e,100);
    require(left.x>0&&right.x<0&&std::abs(left.x+right.x)<1e-5F,"stereo disparity sign/symmetry");
    e.position[0]=0;e.position[2]=-0.5F;a=xr_project_vertex(v,s,e,100);require(std::abs(a.w-50)<1e-5F,"head forward reduces distance");
    e.position[2]=0;e.orientation[1]=std::sin(0.1F);e.orientation[3]=std::cos(0.1F);a=xr_project_vertex(v,s,e,100);
    require(a.x>0&&a.w>0,"inverse head yaw");
    XrMotionHand hand;XrTrackedRay ray;
    require(!xr_tracked_aim_ray(hand,ray),"untracked hand is invalid");
    hand.aim_pose_valid=true;hand.aim_position[0]=0.2F;
    require(xr_tracked_aim_ray(hand,ray),"identity aim pose");
    require(std::abs(ray.origin[0]-0.2F)<1e-5F&&std::abs(ray.forward[2]+1.0F)<1e-5F,
            "identity ray points forward from hand");
    e.orientation[1]=0;e.orientation[3]=1;
    const auto point=xr_project_tracked_point({0,0,-1},e);
    require(std::abs(point.x)<1e-5F&&std::abs(point.y)<1e-5F&&point.w>0,
            "tracked point projects in front of both eyes");
    hand.aim_orientation[1]=std::sin(0.785398163F);hand.aim_orientation[3]=std::cos(0.785398163F);
    require(xr_tracked_aim_ray(hand,ray)&&ray.forward[0]<-0.99F&&std::abs(ray.forward[2])<1e-4F,
            "right-hand yaw rotates tracked ray");
    XrMotionInput motion;
    motion.head_pose_valid=true;
    motion.right=hand;
    XrShotQuat composed{};
    require(xr_compose_shot_aim({0,0,0,1},motion,composed),
            "tracked headset-relative shot aim composes");
    require(std::abs(composed[1]-half_sqrt)<1e-5F&&
            std::abs(composed[3]-half_sqrt)<1e-5F,
            "identity head preserves hand yaw");
    const auto fixed=composed;
    motion.head_orientation[1]=half_sqrt;
    motion.head_orientation[3]=half_sqrt;
    require(xr_compose_shot_aim({0,0,0,1},motion,composed)&&composed==fixed,
            "stationary hand aim is independent of headset rotation");
    require(xr_compose_shot_aim({0,half_sqrt,0,half_sqrt},motion,composed),
            "guest camera heading composes with tracked hand");
    require(std::abs(composed[1]-1.0F)<1e-5F&&std::abs(composed[3])<1e-5F,
            "camera yaw and hand yaw compose in world space");
    std::array<float,16> camera{1,0,0,0,0,1,0,0,0,0,1,0,100,200,300,1};
    std::array<float,3> origin{},target{};
    motion.right.aim_orientation[1]=0;motion.right.aim_orientation[3]=1;
    require(xr_world_shot_ray(camera,motion,origin,target),"tracked world target valid");
    require(std::abs(origin[0]-120)<1e-4F&&std::abs(origin[1]-200)<1e-4F&&
            std::abs(target[2]+9700)<1e-3F,"hand origin uses camera position and metres scale");
    const auto fixed_target=target;
    motion.head_orientation[1]=0;motion.head_orientation[3]=1;
    require(xr_world_shot_ray(camera,motion,origin,target)&&target==fixed_target,
            "native target independent of head rotation");
    motion.right.aim_orientation[0]=half_sqrt;motion.right.aim_orientation[3]=half_sqrt;
    require(xr_world_shot_ray(camera,motion,origin,target)&&target[1]>10199&&
            std::abs(target[2]-300)<0.01F,"controller pitch steers native target upward");
    // Guest display X is opposite the early native camera's X. Check the
    // actual renderer round trip, including head rotation and eye translation.
    const std::array<float,16> display{2,0,0,0, 0,3,0,0,
                                     0,0,1,0.5F, -100,-600,280,150};
    std::array<float,16> rendered_camera{};
    require(xr_tracking_to_world(display,rendered_camera),"display basis invertible");
    motion.right.aim_position[0]=0.4F;motion.right.aim_position[1]=-0.3F;
    motion.right.aim_position[2]=-0.2F;
    motion.right.aim_orientation[0]=0;motion.right.aim_orientation[1]=std::sin(0.25F);
    motion.right.aim_orientation[3]=std::cos(0.25F);
    require(xr_world_shot_ray(rendered_camera,motion,origin,target),"render-basis shot valid");
    XrTrackedRay tracked;require(xr_tracked_aim_ray(motion.right,tracked),"tracked fixture");
    std::array<float,3> world{},room{};
    for(int i=0;i<3;++i) {
        world[i]=origin[i]+(target[i]-origin[i])*0.02F;
        room[i]=tracked.origin[i]+2*tracked.forward[i];
    }
    ::xr64::N64RawFast3DVertex projected;
    projected.x=display[12];projected.y=display[13];projected.w=display[15];
    for(int i=0;i<3;++i) {
        projected.x+=world[i]*display[i*4];
        projected.y+=world[i]*display[i*4+1];
        projected.w+=world[i]*display[i*4+3];
    }
    e.position[0]=0.032F;e.position[1]=0.1F;
    e.orientation[1]=std::sin(0.3F);e.orientation[3]=std::cos(0.3F);
    const auto shot_screen=xr_project_vertex(projected,s,e,100);
    const auto ray_screen=xr_project_tracked_point(room,e);
    require(std::abs(shot_screen.x/shot_screen.w-ray_screen.x/ray_screen.w)<1e-5F&&
            std::abs(shot_screen.y/shot_screen.w-ray_screen.y/ray_screen.w)<1e-5F,
            "world shot and hand ray coincide through real XR renderer");
    std::array<float,16> native_camera{-1,0,0,0,0,1,0,0,0,0,-1,0,50,200,-300,1};
    std::array<float,16> mesh=native_camera,tracked_mesh{};
    for(int row=0;row<3;++row) for(int i=0;i<3;++i) mesh[row*4+i]*=0.05F;
    require(xr_tracked_weapon_matrix(native_camera,rendered_camera,motion,mesh,tracked_mesh),
            "weapon root tracks valid hand pose");
    for(int i=0;i<3;++i) {
        require(std::abs(tracked_mesh[12+i]-origin[i])<1e-4F,"weapon pivot and shot origin coincide");
        require(std::abs(-tracked_mesh[8+i]/0.05F-(target[i]-origin[i])/10000)<1e-5F,
                "weapon forward and shot forward agree");
    }
    const auto fixed_mesh=tracked_mesh;
    motion.head_orientation[1]=std::sin(0.5F);motion.head_orientation[3]=std::cos(0.5F);
    require(xr_tracked_weapon_matrix(native_camera,rendered_camera,motion,mesh,tracked_mesh)&&
            tracked_mesh==fixed_mesh,"head-only turn leaves weapon root fixed");
    motion.head_pose_valid=false;
    require(!xr_compose_shot_aim({0,0,0,1},motion,composed),
            "untracked head cannot steer a shot");
    hand.aim_orientation[0]=std::nanf("");
    require(!xr_tracked_aim_ray(hand,ray),"invalid orientation is rejected");
    XrEyeFrame left_eye{},right_eye{};
    for(auto* eye:{&left_eye,&right_eye}) {eye->fov[0]=-0.785398F;eye->fov[1]=0.785398F;eye->fov[2]=0.785398F;eye->fov[3]=-0.785398F;}
    left_eye.position[0]=-0.032F;right_eye.position[0]=0.032F;
    const auto lc=xr_menu_corner(0,0,left_eye),rc=xr_menu_corner(0,0,right_eye);
    require(std::abs(lc.w-2)<1e-5F && std::abs(rc.w-2)<1e-5F,"menu plane is two metres away in both eyes");
    require(lc.x/lc.w>0 && rc.x/rc.w<0,"menu has correct finite-distance binocular convergence");
    const auto edge=xr_menu_corner(1,1,left_eye);
    require(std::abs(edge.x/edge.w)<0.6F && std::abs(edge.y/edge.w)<0.4F,"menu fits comfortably within eye field");
    ::xr64::N64RawFast3DDrawState white{};white.fill_color={255,255,255,255};
    require(xr_startup_white_fill(true,0,0,319,239,320,240,white),"startup full-screen white fill recognized");
    require(!xr_startup_white_fill(false,0,0,320,240,320,240,white),"gameplay white effects preserved");
    require(!xr_startup_white_fill(true,5,5,100,100,320,240,white),"white menu detail preserved");
    white.textured=true;require(!xr_startup_white_fill(true,0,0,320,240,320,240,white),"splash artwork preserved");
    white.textured=false;
    white.fill_color={255,255,255,141};
    std::vector<std::uint8_t> startup_ram((0x140225U ^ 3U)+1U,0);
    require(!startup_state(nullptr,0),"missing task memory cannot classify startup");
    require(!startup_state(startup_ram.data(),0x140225U ^ 3U),
            "short task memory cannot read the startup byte");
    const bool captured_startup=startup_state(startup_ram.data(),startup_ram.size());
    startup_ram[0x140225U ^ 3U]=1;
    require(captured_startup && !startup_state(startup_ram.data(),startup_ram.size()),
            "startup classification belongs to the decoded task when live guest state changes");
    const auto black=startup_rectangle_fill(true,captured_startup,0,0,319,239,320,240,white);
    require(black.r==0 && black.g==0 && black.b==0 && black.a==141,
            "startup fill is black and preserves alpha in the shared desktop/XR path");
    const auto playing=startup_rectangle_fill(false,true,0,0,319,239,320,240,white);
    require(playing.r==255 && playing.g==255 && playing.b==255 && playing.a==141,
            "gameplay does not inherit startup fill replacement");
    const auto after_startup=startup_rectangle_fill(true,false,0,0,319,239,320,240,white);
    require(after_startup.r==255 && after_startup.a==141,
            "a menu after startup keeps its full-screen white fill");
    const auto detail=startup_rectangle_fill(true,true,5,5,100,100,320,240,white);
    require(detail.r==255 && detail.a==141,"partial menu art keeps its fill color");
    white.fill_color.r=239;
    const auto colored=startup_rectangle_fill(true,true,0,0,319,239,320,240,white);
    require(colored.r==239 && colored.g==255 && colored.a==141,
            "colored full-screen fills are preserved");
    white.fill_color={255,255,255,141};white.textured=true;
    const auto artwork=startup_rectangle_fill(true,true,0,0,319,239,320,240,white);
    require(artwork.r==255 && artwork.a==141,"textured splash artwork is preserved");

    std::puts("XR camera and shot quaternion math passed; game shot ownership and alignment require runtime verification.");
}
