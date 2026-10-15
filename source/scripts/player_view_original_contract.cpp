#include "player_view.hpp"
#include "recomp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include <xmmintrin.h>
#pragma fenv_access(on)

using namespace rage_wars::player;
namespace {
constexpr std::uint32_t Actor = 0x80100000, Source = 0x80102000;
constexpr std::uint32_t Camera = 0x80110000, OtherCamera = 0x80111000;
constexpr std::uint32_t Region = 0x80130000, OtherRegion = 0x80131000;
constexpr std::uint32_t Stack = 0x807F0000;
constexpr std::array<std::uint32_t,14> GlobalAddresses{
    0x800C26D4,0x800C26D8,0x800C26DC,0x800C26E0,0x800C26E4,0x800C26E8,
    0x800C26EC,0x800C26F0,0x800C3450,0x800C3454,0x800C3458,0x800C9EB4,
    0x800C9EB8,0x800CD7E0};
std::uint32_t bits(float x) { return std::bit_cast<std::uint32_t>(x); }
float number(std::uint32_t x) { return std::bit_cast<float>(x); }
gpr guest(std::uint32_t x) { return static_cast<gpr>(static_cast<std::int32_t>(x)); }
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
struct Event {
    std::string tag;
    std::vector<std::uint32_t> words;
    bool operator==(const Event&) const = default;
};
struct Mutation { std::string stage; std::uint32_t address, value; };
struct Plan {
    std::array<std::uint32_t,3> query{0,0,0};
    std::uint32_t located = Region, height = bits(10.0f);
    std::uint32_t first = bits(0.75f), second = bits(-0.25f);
    std::vector<Mutation> mutations;
    std::vector<std::pair<std::string,int>> rounding_mutations;
};
class World final : public WordStorage, public ViewGlobals, public ViewCallbacks {
public:
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x800000);
    Plan plan;
    std::vector<Event> trace;
    std::uint32_t actor = Actor, source = Actor, camera = Camera;
    unsigned query_count = 0;
    bool tracking = false;
    bool reorder_native_stores = false, pending_rotation = false;
    std::uint32_t pending_address = 0, pending_value = 0;
    std::uint32_t read(std::uint32_t address) const {
        const auto offset = address & 0x7FFFFF;
        require(offset + 4 <= ram.size() && !(offset & 3), "invalid aligned guest read");
        std::uint32_t value; std::memcpy(&value, ram.data()+offset,4); return value;
    }
    void put(std::uint32_t address, std::uint32_t value) {
        const auto offset = address & 0x7FFFFF;
        require(offset + 4 <= ram.size() && !(offset & 3), "invalid aligned guest write");
        // Guest ABI frame bookkeeping is checked separately, not a game store.
        const bool stack = offset >= (Stack & 0x7FFFFF)-0x200 && offset < (Stack & 0x7FFFFF)+0x100;
        if (tracking && !stack) trace.push_back({"store",{address,value}});
        std::memcpy(ram.data()+offset,&value,4);
    }
    void scalar_at(std::uint32_t address,float x) { put(address,bits(x)); }
    std::uint32_t load_word(ObjectId id,std::uint32_t offset) const override { require(id!=0,"native null object field read"); return read(id+offset); }
    void store_word(ObjectId id,std::uint32_t offset,std::uint32_t x) override {
        // Negative control preserves final memory but reverses two semantic stores.
        if(reorder_native_stores && id==camera && offset==0x28) {
            pending_rotation=true;pending_address=id+offset;pending_value=x;return;
        }
        put(id+offset,x);
        if(reorder_native_stores && pending_rotation && id==camera && offset==0x2C) {
            put(pending_address,pending_value);pending_rotation=false;
        }
    }
    std::uint32_t load_word(ViewGlobal id) const override { return read(GlobalAddresses[static_cast<unsigned>(id)]); }
    void store_word(ViewGlobal id,std::uint32_t x) override { put(GlobalAddresses[static_cast<unsigned>(id)],x); }
    void callback(const std::string& tag,std::vector<std::uint32_t> arguments={}) {
        arguments.push_back(static_cast<std::uint32_t>(std::fegetround()));
        arguments.push_back(camera);
        if (camera) for (unsigned offset=0x24;offset<=0x64;offset+=4) arguments.push_back(read(camera+offset));
        for (auto address:GlobalAddresses) arguments.push_back(read(address));
        trace.push_back({tag,std::move(arguments)});
        for (const auto& effect:plan.mutations) if(effect.stage==tag) put(effect.address,effect.value);
        for (const auto& change:plan.rounding_mutations) if(change.first==tag)std::fesetround(change.second);
    }
    StateQuery original_query_phase() const {
        if(query_count==0) return StateQuery::before_region_lookup;
        if(query_count==1 && read(camera+0x58)!=0) return StateQuery::before_height;
        return StateQuery::final_visibility;
    }
    std::uint32_t query(StateQuery phase) {
        const unsigned logical = static_cast<unsigned>(phase);
        require(query_count < 3, "excess state callbacks");
        static const char* names[]{"state.lookup","state.height","state.final"};
        const auto result=plan.query[logical];
        callback(names[logical],{logical,result}); ++query_count; return result;
    }
    bool state_query(StateQuery q) override { return query(q)!=0; }
    ObjectId locate_region(ObjectId id) override {
        require(id==camera,"lookup did not retain captured camera");
        callback("locate",{0x80119DC8,id+0x38}); return plan.located;
    }
    float region_height(ObjectId region,float x,float z) override {
        callback("height",{region,bits(x),bits(z)}); return number(plan.height);
    }
    float first_angle_function(float yaw) override { callback("first",{bits(yaw)}); return number(plan.first); }
    float second_angle_function(float yaw) override { callback("second",{bits(yaw)}); return number(plan.second); }
    void update_camera(ObjectId id) override { callback("camera.update",{id}); }
    void update_actor(ObjectId id) override { callback("actor.update",{id}); }
};
thread_local World* Active = nullptr;
struct OriginalWord {
    std::uint32_t address;
    operator std::int32_t() const { return static_cast<std::int32_t>(Active->read(address)); }
    OriginalWord& operator=(std::uint32_t value) { Active->put(address,value); return *this; }
};
OriginalWord original_word(std::uint8_t* ram,gpr address) {
    require(Active && Active->ram.data()==ram,"oracle memory owner mismatch");
    return {static_cast<std::uint32_t>(address)};
}
void poison_callers(recomp_context* ctx) {
    // Actual original bodies must not accidentally depend on mocked ABI preservation.
    gpr* regs=&ctx->r0;
    for(unsigned r=1;r<=15;++r) regs[r]=guest(0xC1000000+r);
    auto* fp=&ctx->f0;
    for(unsigned r=0;r<20;++r) fp[r].u64=0x3FF0000000000000ULL+r;
}
void original_height(std::uint8_t*,recomp_context* ctx) {
    const auto result=Active->region_height(static_cast<std::uint32_t>(ctx->r4),
        number(static_cast<std::uint32_t>(ctx->r5)),number(static_cast<std::uint32_t>(ctx->r6)));
    poison_callers(ctx);ctx->f0.u32l=bits(result);
}
}

// Bodies are included byte-for-byte from pinned current owner files. Only their
// external memory seam records stores; the original target branch bodies remain intact.
#undef MEM_W
#define MEM_W(offset,reg) original_word(rdram,(reg)+(offset))
void trace_func_00220A5C_r0002165C(std::uint8_t*,recomp_context*);
void trace_func_00238F04_r00039B04(std::uint8_t*,recomp_context*);
void resident_wave9_func_00245778(std::uint8_t*,recomp_context* ctx) {
    const auto result=Active->query(Active->original_query_phase());
    poison_callers(ctx);ctx->r2=guest(result);
}
void resident_wave14_func_00286678(std::uint8_t*,recomp_context* ctx) {
    require(static_cast<std::uint32_t>(ctx->r4)==0x80119DC8,"wrong original lookup owner");
    const auto camera=static_cast<std::uint32_t>(ctx->r5)-0x38;
    const auto result=Active->locate_region(camera);poison_callers(ctx);ctx->r2=guest(result);
}
void resident_wave9_func_002B7060(std::uint8_t*,recomp_context* ctx) {
    const auto result=Active->first_angle_function(ctx->f12.fl);poison_callers(ctx);ctx->f0.u32l=bits(result);
}
void resident_wave14_func_002B6490(std::uint8_t*,recomp_context* ctx) {
    const auto result=Active->second_angle_function(ctx->f12.fl);poison_callers(ctx);ctx->f0.u32l=bits(result);
}
void resident_wave14_func_00234FCC(std::uint8_t*,recomp_context* ctx) {
    Active->update_camera(static_cast<std::uint32_t>(ctx->r4));poison_callers(ctx);
}
void trace_func_0021D750_r0001E350(std::uint8_t*,recomp_context* ctx) {
    Active->update_actor(static_cast<std::uint32_t>(ctx->r4));poison_callers(ctx);
}
extern "C" recomp_func_t* get_function_with_context(std::uint8_t*,recomp_context*,std::int32_t address) {
    require(static_cast<std::uint32_t>(address)==0x00275DC4,"unexpected original indirect dependency");
    return original_height;
}
#include "generated_original.inc"

namespace {
World baseline(bool separate=false) {
    World world;world.source=separate?Source:Actor;
    for(auto id:{Actor,Source}) {
        world.scalar_at(id+0x08,3.0f);world.scalar_at(id+0x0C,10.0f);world.scalar_at(id+0x10,4.0f);
        world.put(id+0x14,Region);world.scalar_at(id+0x6C,1.25f);
        for(unsigned i=0;i<4;++i) world.put(id+0x5C+i*4,std::array<std::uint32_t,4>{0x80000000,0x7FC12345,0xFFFFFFFF,0x3F800000}[i]);
    }
    world.put(Actor+0x5D0,1);world.put(Actor+0x5DC,Camera);world.put(Actor+0x6AC,0);
    for(auto [offset,value]:std::array<std::pair<unsigned,float>,13>{{
        {0x708,.5f},{0x718,.25f},{0x720,.5f},{0x724,2.f},{0x728,.25f},{0x72C,-1.f},
        {0x730,.5f},{0x734,.125f},{0x738,2.f},{0x73C,.25f},{0x740,2.f},{0x744,1.f},{0x780,.5f}}})
        world.scalar_at(Actor+offset,value);
    world.scalar_at(Actor+0x7EC,0.0f);
    for(auto id:{Camera,OtherCamera})for(unsigned offset=0x24;offset<=0x64;offset+=4)world.put(id+offset,0xABCD0000+offset);
    world.put(Region+0x1C,0x0BADCAFE);world.put(OtherRegion+0x1C,0xDEADBEEF);
    std::array<float,13> values{1.5f,3.f,.25f,.125f,.75f,.5f,2.f,7.f,.5f,2.f,-2.f,.125f,4.f};
    for(unsigned i=0;i<values.size();++i)world.put(GlobalAddresses[i],bits(values[i]));
    world.put(GlobalAddresses.back(),0x12345678);return world;
}
ViewParameters direct_parameters(const World& world) {
    (void)world;ViewParameters p;p.view_type=7;p.rotation_28=0x80000000;p.rotation_2c=bits(.75f);
    p.parameter_30=.5f;p.parameter_34=-.25f;p.x=3.f;p.y=10.f;p.z=4.f;p.eye=1.f;
    p.orientation={0x80000000,0x7FA12345,0xFFFFFFFF,0x3F800000};p.region=Region;p.blend=0.f;p.trailing_60=3.f;return p;
}
void original_run(World& world,bool commit,const ViewParameters& p) {
    recomp_context ctx{};ctx.mips3_float_mode=1;ctx.f_odd=&ctx.f1.u32l;
    ctx.r29=guest(Stack);ctx.r31=guest(0x81234564);
    ctx.r16=guest(0x91110010);ctx.r17=guest(0x92220020);ctx.r18=guest(0x93330030);
    ctx.f20.u64=0x3FF2345600000020ULL;ctx.f21.u64=0x3FF3456700000021ULL;
    ctx.f22.u64=0x3FF4567800000022ULL;ctx.f23.u64=0x3FF5678900000023ULL;
    const auto saved=ctx;
    if(commit) {
        ctx.r4=guest(world.camera);ctx.r5=guest(p.view_type);ctx.r6=guest(p.rotation_28);ctx.r7=guest(p.rotation_2c);
        std::array<std::uint32_t,13> words{bits(p.parameter_30),bits(p.parameter_34),bits(p.x),bits(p.y),bits(p.z),bits(p.eye),
            p.orientation[0],p.orientation[1],p.orientation[2],p.orientation[3],p.region,bits(p.blend),bits(p.trailing_60)};
        for(unsigned i=0;i<words.size();++i)world.put(Stack+0x10+i*4,words[i]);
    } else {ctx.r4=guest(world.actor);ctx.r5=guest(world.source);}
    world.camera=commit?world.camera:world.read(world.actor+0x5DC);
    world.tracking=true;Active=&world;
    if(commit)trace_func_00238F04_r00039B04(world.ram.data(),&ctx);
    else trace_func_00220A5C_r0002165C(world.ram.data(),&ctx);
    Active=nullptr;
    require(ctx.r29==saved.r29 && ctx.r31==saved.r31 && ctx.r16==saved.r16 && ctx.r17==saved.r17 && ctx.r18==saved.r18,"original GPR/frame ABI failed");
    require(ctx.f20.u64==saved.f20.u64 && ctx.f21.u64==saved.f21.u64 && ctx.f22.u64==saved.f22.u64 && ctx.f23.u64==saved.f23.u64,"original saved FP ABI failed");
}
unsigned Passed=0;
bool NegativeControl=false;
void run_case(const std::string& name,const World& initial,bool commit=false,ViewParameters p={}) {
    World original=initial,native=initial;
    std::fenv_t entry_env;std::fegetenv(&entry_env);
    original_run(original,commit,p);
    const auto original_rounding=std::fegetround();std::fesetenv(&entry_env);
    native.camera=commit?native.camera:native.read(native.actor+0x5DC);
    native.tracking=true;native.reorder_native_stores=NegativeControl;RageWarsActorStorage storage(native);PlayerView view(storage,native,native);
    if(commit)view.commit(native.camera,p);else view.update(native.actor,native.source);
    require(std::fegetround()==original_rounding,"callback rounding environment mismatch");std::fesetenv(&entry_env);
    if(original.trace!=native.trace) {
        unsigned at=0;while(at<original.trace.size() && at<native.trace.size() && original.trace[at]==native.trace[at])++at;
        std::fprintf(stderr,"%s trace mismatch at %u: original=%s native=%s\n",name.c_str(),at,
            at<original.trace.size()?original.trace[at].tag.c_str():"END",at<native.trace.size()?native.trace[at].tag.c_str():"END");
        if(at<original.trace.size() && at<native.trace.size())for(unsigned i=0;i<std::min(original.trace[at].words.size(),native.trace[at].words.size());++i)
            if(original.trace[at].words[i]!=native.trace[at].words[i])std::fprintf(stderr,"  trace word%u original=%08X native=%08X\n",i,original.trace[at].words[i],native.trace[at].words[i]);
        throw std::runtime_error(name+" callback/store sequence mismatch");
    }
    for(unsigned offset=0;offset<0x800000;offset+=4) {
        if(offset>=(Stack&0x7FFFFF)-0x200 && offset<(Stack&0x7FFFFF)+0x100)continue;
        const auto a=original.read(0x80000000+offset),b=native.read(0x80000000+offset);
        if(a!=b){std::fprintf(stderr,"%s memory mismatch %08X: original=%08X native=%08X\n",name.c_str(),0x80000000+offset,a,b);throw std::runtime_error(name+" exact output mismatch");}
    }
    ++Passed;std::printf("PASS %s\n",name.c_str());
}
void effect(World& world,const char* stage,std::uint32_t address,std::uint32_t value) {world.plan.mutations.push_back({stage,address,value});}
void named_cases() {
    auto w=baseline();run_case("mode1_shared_actor_source",w);
    w=baseline(true);run_case("mode1_distinct_source",w);
    w=baseline();w.put(Actor+0x5DC,0);w.source=0;run_case("null_camera_only_actor_callback",w);
    for(auto mode:{0u,3u,0xFFFFFFFFu}){w=baseline();w.put(Actor+0x5D0,mode);w.source=0;run_case("other_mode_"+std::to_string(mode),w);}
    w=baseline();w.source=Camera;w.scalar_at(Camera+8,3.f);w.scalar_at(Camera+12,10.f);w.scalar_at(Camera+16,4.f);w.scalar_at(Camera+0x6C,1.f);w.put(Camera+0x14,Region);
    run_case("source_aliases_camera_output_snapshot",w);
    w=baseline();w.scalar_at(Actor+0x724,16777216.f);w.scalar_at(Actor+0x730,1.f);w.scalar_at(Actor+0x708,16777216.f);run_case("binary32_vertical_round_before_subtract",w);
    w=baseline();w.scalar_at(Actor+0x740,16777216.f);w.scalar_at(Actor+0x780,1.f);w.scalar_at(Actor+0x718,16777216.f);w.scalar_at(Actor+0x720,0.f);run_case("binary32_eye_round_before_subtract",w);
    for(auto eye:{std::nextafter(1.5f,-INFINITY),1.5f,std::nextafter(1.5f,INFINITY)}) {w=baseline();w.scalar_at(Actor+0x740,eye);w.scalar_at(Actor+0x780,0.f);w.scalar_at(Actor+0x718,0.f);w.scalar_at(Actor+0x720,0.f);run_case("clamp_boundary_"+std::to_string(bits(eye)),w);}
    w=baseline();w.put(Actor+0x6C,0x80000000);w.put(Actor+0x0C,0x80000000);run_case("raw_angle_and_y_negative_zero",w);
    w=baseline();w.put(Actor+0x6C,0x7FA12345);run_case("raw_snan_angle_no_arithmetic_claim",w);
    w=baseline();w.put(Actor+0x740,0x7FC12345);run_case("unordered_eye_release_host_oracle",w);
    w=baseline();w.put(GlobalAddresses[0],0x7FC12345);run_case("unordered_minimum_release_host_oracle",w);
    w=baseline();w.put(Actor+0x14,0);run_case("null_region_skips_middle_query",w);
    for(unsigned mask=0;mask<8;++mask){w=baseline();for(unsigned i=0;i<3;++i)w.plan.query[i]=(mask&(1u<<i))?0xFFFFFFFFu:0;run_case("independent_query_results_"+std::to_string(mask),w);}
    w=baseline();w.plan.query[0]=1;effect(w,"locate",Camera+0x38,bits(9.f));effect(w,"locate",Camera+0x3C,bits(12.f));effect(w,"locate",Camera+0x44,bits(2.f));effect(w,"locate",GlobalAddresses.back(),0xABCDEF01);run_case("lookup_mutates_capture_and_initial_default",w);
    w=baseline();effect(w,"state.height",Camera+0x38,bits(999.f));effect(w,"state.height",Camera+0x3C,bits(999.f));effect(w,"state.height",Camera+0x40,bits(999.f));effect(w,"state.height",Camera+0x44,bits(999.f));effect(w,"state.height",Camera+0x58,OtherRegion);run_case("query2_uses_saved_position_and_region",w);
    w=baseline();effect(w,"height",Region+0x1C,0xFEED1234);effect(w,"height",Camera+0x58,OtherRegion);effect(w,"height",Camera+0x3C,bits(999.f));effect(w,"height",GlobalAddresses[8],bits(.25f));run_case("height_mutates_region_flags_and_scale",w);
    w=baseline();effect(w,"state.final",Camera+0x64,0xCAFE1000);run_case("final_false_keeps_callback_visibility_write",w);
    w=baseline();w.plan.query[2]=1;effect(w,"state.final",GlobalAddresses.back(),0xCAFE2000);run_case("final_true_reads_fresh_default",w);
    w=baseline();effect(w,"state.final",Camera+0x5C,bits(.25f));effect(w,"state.final",GlobalAddresses.back(),0xCAFE3000);run_case("final_callback_arms_positive_current_blend",w);
    w=baseline();w.scalar_at(Actor+0x7EC,.25f);effect(w,"state.final",Camera+0x5C,bits(-.25f));run_case("final_callback_clears_positive_incoming_blend",w);
    for(auto blend:{0x80000000u,0x00000000u,0xBF800000u,0x3F800000u,0x7F800000u,0x7FC12345u}) {w=baseline();w.put(Actor+0x7EC,blend);run_case("blend_gate_bits_"+std::to_string(blend),w);}
    w=baseline();effect(w,"state.lookup",Actor+0x5DC,OtherCamera);effect(w,"camera.update",Actor+0x08,bits(-77.f));run_case("camera_handle_capture_and_final_actor_order",w);
    for(unsigned flags=0;flags<16;++flags){w=baseline();w.put(Actor+0x5D0,2);const auto mask=((flags&1)?0x200:0)|((flags&2)?0x100:0)|((flags&4)?0x800:0)|((flags&8)?0x400:0);w.put(Actor+0x6AC,mask);run_case("mode2_input_priority_"+std::to_string(mask),w);}
    w=baseline();w.put(Actor+0x5D0,2);effect(w,"first",GlobalAddresses[12],bits(6.f));effect(w,"first",Actor+0x08,bits(31.f));effect(w,"first",Actor+0x6C,bits(99.f));effect(w,"first",GlobalAddresses[11],bits(100.f));effect(w,"second",GlobalAddresses[12],bits(9.f));effect(w,"second",Actor+0x08,bits(81.f));effect(w,"second",Actor+0x10,bits(41.f));effect(w,"second",Actor+0x0C,bits(51.f));effect(w,"second",Actor+0x5C,0x12345678);effect(w,"second",Actor+0x14,OtherRegion);run_case("mode2_callback_reloads_and_saved_yaw_x",w);
    w=baseline();w.put(Actor+0x5D0,2);w.put(GlobalAddresses[12],0x3F800001);w.plan.first=0x3F7FFFFF;w.scalar_at(Actor+0x08,-1.f);run_case("separate_mul_add_rounding_not_fma",w);
    w=baseline();w.put(Actor+0x728,0x00800000);w.put(Actor+0x734,0x807FFFFF);run_case("controlled_gradual_underflow_host_oracle",w);
    w=baseline();w.put(Actor+0x5D0,2);w.plan.rounding_mutations={{"first",FE_UPWARD},{"second",FE_DOWNWARD}};w.put(GlobalAddresses[12],0x3F800001);w.plan.first=0x3F7FFFFF;w.plan.second=0x3F7FFFFF;run_case("math_callbacks_change_host_rounding",w);
    auto p=direct_parameters(w);
    for(auto delta:{std::nextafter(-2.f,-INFINITY),-2.f,std::nextafter(-2.f,INFINITY),std::nextafter(2.f,-INFINITY),2.f,std::nextafter(2.f,INFINITY)}) {w=baseline();p=direct_parameters(w);p.y=delta;p.eye=0;w.plan.height=0;w.put(GlobalAddresses[8],bits(1.f));run_case("strict_visibility_window_"+std::to_string(bits(delta)),w,true,p);}
    for(auto which:{8u,9u,10u}) {w=baseline();p=direct_parameters(w);w.put(GlobalAddresses[which],0x7FC12345);run_case("unordered_visibility_constant_"+std::to_string(which),w,true,p);}
    w=baseline();p=direct_parameters(w);w.plan.height=0x7FC12345;run_case("unordered_height_release_host_oracle",w,true,p);
    w=baseline();p=direct_parameters(w);p.region=Camera+8;run_case("region_flags_alias_committed_view_type",w,true,p);
}
void random_finite_cases() {
    std::uint32_t state=0x13579BDF;
    auto next=[&](){state^=state<<13;state^=state>>17;state^=state<<5;return state;};
    for(unsigned iteration=0;iteration<32;++iteration) {
        auto w=baseline((iteration&1)!=0);w.put(Actor+0x5D0,(iteration&2)?2:1);
        for(auto offset:{0x708,0x718,0x720,0x724,0x728,0x72C,0x730,0x734,0x738,0x73C,0x740,0x744,0x780,0x7EC})w.scalar_at(Actor+offset,static_cast<float>(static_cast<int>(next()%2049)-1024)/8.f);
        for(auto offset:{8,12,16,0x6C})w.scalar_at(w.source+offset,static_cast<float>(static_cast<int>(next()%2049)-1024)/8.f);
        for(auto& q:w.plan.query)q=(next()&1)?1:0;
        w.plan.height=bits(static_cast<float>(static_cast<int>(next()%257)-128)/8.f);
        run_case("seeded_finite_differential_"+std::to_string(iteration),w);
    }
}
void captured_seed(const std::string& file,std::uint32_t actor,bool big_endian) {
    std::ifstream in(file,std::ios::binary);require(static_cast<bool>(in),"captured seed unavailable");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    require(bytes.size()==0x400000 || bytes.size()==0x800000,"seed is not admitted 4/8 MiB");
    World w;w.actor=w.source=actor;
    if(big_endian)for(unsigned offset=0;offset<bytes.size();offset+=4){const std::uint32_t word=(std::uint32_t(bytes[offset])<<24)|(std::uint32_t(bytes[offset+1])<<16)|(std::uint32_t(bytes[offset+2])<<8)|bytes[offset+3];std::memcpy(w.ram.data()+offset,&word,4);}
    else std::copy(bytes.begin(),bytes.end(),w.ram.begin());
    w.camera=w.read(actor+0x5DC);w.plan.located=w.read(actor+0x14);
    run_case("historical_captured_state_synthetic_body_replay",w);
}
}
int main(int argc,char** argv) {
    const auto saved_mxcsr=_mm_getcsr();std::fenv_t saved_env;std::fegetenv(&saved_env);
    std::fesetenv(FE_DFL_ENV);std::fesetround(FE_TONEAREST);_mm_setcsr(_mm_getcsr()&~(0x8000u|0x40u));
    try {
        NegativeControl=argc==2 && std::string(argv[1])=="--negative-control";
        named_cases();random_finite_cases();
        for(auto round:{FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){std::fesetround(round);auto w=baseline();w.scalar_at(Actor+0x724,16777216.f);w.scalar_at(Actor+0x730,1.f);w.scalar_at(Actor+0x708,16777216.f);run_case("host_rounding_environment_"+std::to_string(round),w);}
        std::fesetround(FE_TONEAREST);
        if(argc==4)captured_seed(argv[1],static_cast<std::uint32_t>(std::stoul(argv[2],nullptr,0)),std::string(argv[3])=="big");
        else require(argc==1,"expected optional seed path, actor address, byte order");
        std::printf("TOTAL_PASS %u; SYNTHETIC_ORIGINAL_BODY_DIFFERENTIAL; NDEBUG_RELEASE_ORACLE; STRICT_FP\n",Passed);
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());std::fesetenv(&saved_env);_mm_setcsr(saved_mxcsr);return 1;}
    std::fesetenv(&saved_env);_mm_setcsr(saved_mxcsr);return 0;
}
