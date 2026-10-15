#include "rage_wars_player_view_bridge.h"
#include "funcs.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t actor = 0x00200000; // Deliberately translated KUSEG ID.
constexpr std::uint32_t camera = 0x80204000, region = 0x80206000;
std::vector<std::uint8_t> words(8 * 1024 * 1024);
std::vector<unsigned> events;
bool throw_on_camera = false;
constexpr std::uint32_t incoming_sp = 0x807FF000;
std::uint32_t physical(std::uint32_t a) {
    if (a >= 0x80000000 && a < 0x80800000) return a - 0x80000000;
    if (a >= actor && a < actor + 0x2000) return a;
    throw std::runtime_error("Unmapped fixture address");
}
void put(std::uint32_t at, std::uint32_t value) {
    std::memcpy(words.data() + physical(at), &value, 4);
}
std::uint32_t get(std::uint32_t at) {
    std::uint32_t value; std::memcpy(&value, words.data() + physical(at), 4); return value;
}
void scalar(std::uint32_t at, float value) { put(at, std::bit_cast<std::uint32_t>(value)); }
void need(bool value) { if (!value) throw std::runtime_error("Invalid bridge callback ABI"); }
void seed() {
    std::fill(words.begin(), words.end(), 0);
    put(actor+0x5D0,1); put(actor+0x5DC,camera); put(actor+0x1450,0);
    put(actor+0x14,region);
    scalar(actor+8,10); scalar(actor+12,20); scalar(actor+16,30); scalar(actor+0x6C,0.25f);
    for(auto offset : {0x708,0x718,0x720,0x724,0x728,0x72C,0x730,0x734,0x738,0x73C,0x740,0x744,0x780}) scalar(actor+offset,1);
    scalar(actor+0x7EC,0);
    for(unsigned i=0;i<4;++i) put(actor+0x5C+i*4,0x80000001U+i);
    scalar(0x800C26D4,0); scalar(0x800C26D8,2);
    scalar(0x800C3450,1); scalar(0x800C3454,100); scalar(0x800C3458,-100);
    put(0x800CD7E0,0x11111111); put(region+0x1C,0x22222222);
    events.clear(); throw_on_camera=false;
}
recomp_context context() {
    recomp_context c{};
    c.mips3_float_mode=1; c.f_odd=&c.f1.u32l;
    c.r4=actor;c.r5=actor;c.r29=(gpr)(int32_t)incoming_sp;c.r31=0x12345678;
    c.r16=11;c.r17=12;c.r18=13;c.f20.u64=0xABCDEF0123456789ULL;
    return c;
}
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* memory, gpr address) {
    return memory + physical(static_cast<std::uint32_t>(address));
}
extern "C" void resident_wave9_func_00245778(std::uint8_t*, recomp_context* c) {
    need((uint32_t)c->r29 == incoming_sp-0xA8);
    events.push_back(1);c->r2=0;
}
extern "C" void resident_wave14_func_00286678(std::uint8_t*, recomp_context*) {
    throw std::runtime_error("Unexpected region lookup");
}
extern "C" void resident_wave9_func_002B7060(std::uint8_t*, recomp_context*) {
    throw std::runtime_error("Mode2 helper entered attached-only bridge");
}
extern "C" void resident_wave14_func_002B6490(std::uint8_t*, recomp_context*) {
    throw std::runtime_error("Mode2 helper entered attached-only bridge");
}
extern "C" void height_callback(std::uint8_t*, recomp_context* c) {
    need((uint32_t)c->r29==incoming_sp-0xA8 && (uint32_t)c->r4==region);
    need((uint32_t)c->r5==std::bit_cast<uint32_t>(11.0f));
    need((uint32_t)c->r6==std::bit_cast<uint32_t>(31.0f));
    events.push_back(2);c->f0.fl=20.0f;
}
extern "C" recomp_func_t* get_function_with_context(std::uint8_t*, recomp_context*, int32_t at) {
    need(at==0x00275DC4);return height_callback;
}
extern "C" void resident_wave14_func_00234FCC(std::uint8_t*, recomp_context* c) {
    need((uint32_t)c->r29==incoming_sp-0x68 && (uint32_t)c->r4==camera);
    events.push_back(3);
    c->r16=0xDEADBEEF;c->f20.u64=0;
    if(throw_on_camera) throw std::runtime_error("Injected callback failure");
}
extern "C" void trace_func_0021D750_r0001E350(std::uint8_t*, recomp_context* c) {
    need((uint32_t)c->r29==incoming_sp-0x68 && (uint32_t)c->r4==actor);
    events.push_back(4);c->r31=0xBAD;c->r5=0xBAD;
}

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char**) {
    const bool expected_enabled=argc>1;
    seed();auto c=context();c.f_odd=&c.f1.u32l;const auto saved=c;
    const int handled=xr64_player_view_try_update(words.data(),&c);
    REQUIRE(std::memcmp(&c,&saved,sizeof(c))==0);
    if(!expected_enabled) {
        REQUIRE(handled==0 && events.empty() && get(camera+0x24)==0 && get(camera+0x64)==0);
        std::puts("PASS default preserves original route/context/storage");return 0;
    }
    REQUIRE(handled==1 && events==std::vector<unsigned>({1,1,2,1,3,4}));
    REQUIRE(get(camera+0x64)==0x22222222 && get(camera+0x28)==get(actor+0x6C));
    for(unsigned i=0;i<4;++i) REQUIRE(get(camera+0x48+i*4)==get(actor+0x5C+i*4));
    const auto expect_fallback=[&](auto modify) {
        seed();auto ctx=context();ctx.f_odd=&ctx.f1.u32l;modify(ctx);auto prior=ctx;
        auto before=words;
        return xr64_player_view_try_update(words.data(),&ctx)==0 && events.empty()
            && std::memcmp(&ctx,&prior,sizeof(ctx))==0 && words==before;
    };
    REQUIRE(expect_fallback([](auto& c){c.r5=actor+4;}));
    REQUIRE(expect_fallback([](auto&){put(actor+0x5D0,2);}));
    REQUIRE(expect_fallback([](auto&){put(actor+0x5DC,0);}));
    REQUIRE(expect_fallback([](auto&){put(actor+0x1450,1);}));
    REQUIRE(expect_fallback([](auto& c){c.mips3_float_mode=0;c.f_odd=&c.f0.u32h;}));
    seed();c=context();c.f_odd=&c.f1.u32l;const auto before_throw=c;throw_on_camera=true;
    bool threw=false;
    try { (void)xr64_player_view_try_update(words.data(),&c); }
    catch(const std::runtime_error&) { threw=true; }
    REQUIRE(threw && events==std::vector<unsigned>({1,1,2,1,3}));
    REQUIRE(std::memcmp(&c,&before_throw,sizeof(c))==0);
    REQUIRE(get(camera+0x64)==0x22222222); // Propagated failure, no original replay.
    std::puts("PASS translated shared storage, mode1 helper ABI/order, exact orientation, context restore, five fallback guards, exception propagation");
}