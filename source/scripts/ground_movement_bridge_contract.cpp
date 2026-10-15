#include "rage_wars_ground_movement_bridge.h"
#include "funcs.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t actor = 0x00200000; // Deliberately translated KUSEG ID.
constexpr std::uint32_t params = 0x800C9400;
constexpr std::uint32_t incoming_sp = 0x807FF000, frame = incoming_sp - 0x48, slot = frame + 0x18;
std::vector<std::uint8_t> words(8 * 1024 * 1024);
std::vector<unsigned> events;
bool throw_on_friction = false;
float last_scaled[3]{};
std::uint32_t physical(std::uint32_t a) {
    if (a >= 0x80000000 && a < 0x80800000) return a - 0x80000000;
    if (a >= actor && a < actor + 0x2000) return a;
    throw std::runtime_error("Unmapped fixture address");
}
void put(std::uint32_t at, std::uint32_t v) { std::memcpy(words.data() + physical(at), &v, 4); }
std::uint32_t get(std::uint32_t at) { std::uint32_t v; std::memcpy(&v, words.data() + physical(at), 4); return v; }
void scalar(std::uint32_t at, float v) { put(at, std::bit_cast<std::uint32_t>(v)); }
float number(std::uint32_t at) { return std::bit_cast<float>(get(at)); }
void need(bool v) { if (!v) throw std::runtime_error("Invalid bridge callback ABI"); }
void in_frame(recomp_context* c) { need(static_cast<std::uint32_t>(c->r29) == frame); }
void seed() {
    std::fill(words.begin(), words.end(), 0);
    scalar(actor + 8, 10); scalar(actor + 12, 20); scalar(actor + 16, 30); scalar(actor + 0x6C, .25f);
    scalar(actor + 0x6A8, 1); scalar(actor + 0x6A4, 0); put(actor + 0x6B0, 8); put(actor + 0x6B8, 0);
    scalar(actor + 0x66C, 2); scalar(actor + 0x6E8, 100); scalar(actor + 0x6EC, 200); scalar(actor + 0x6F0, 300);
    for (unsigned i = 0; i < 8; ++i) scalar(params + i * 4, 1.0f + i);
    put(params + 0x20, 0x0000FFF0); // sound id -16
    for (std::uint32_t a = 0x800C27E4; a <= 0x800C280C; a += 4) scalar(a, .5f);
    scalar(0x800CD648, 1.0f / 30.0f);
    events.clear(); throw_on_friction = false;
}
recomp_context context() {
    recomp_context c{};
    c.mips3_float_mode = 1; c.f_odd = &c.f1.u32l;
    c.r4 = actor; c.r5 = actor; c.r6 = (gpr)(int32_t)params;
    c.r29 = (gpr)(int32_t)incoming_sp; c.r31 = 0x12345678;
    c.r16 = 11; c.r17 = 12; c.r18 = 13; c.f20.u64 = 0xABCDEF0123456789ULL;
    return c;
}
void clobber(recomp_context* c) { c->r16 = 0xDEADBEEF; c->r31 = 0xBAD; c->f20.u64 = 0; }
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* memory, gpr address) {
    return memory + physical(static_cast<std::uint32_t>(address));
}
extern "C" void resident_wave9_func_00245764(std::uint8_t*, recomp_context* c) {
    in_frame(c); events.push_back(1); clobber(c); c->r2 = 0;
}
extern "C" void trace_func_00274690_r00075290(std::uint8_t*, recomp_context* c) {
    in_frame(c); need(c->f14.fl == 1.0f && static_cast<std::uint32_t>(c->r6) == std::bit_cast<std::uint32_t>(2.0f));
    events.push_back(2); clobber(c); c->f0.fl = 3.0f;
}
extern "C" void resident_wave14_func_00274790(std::uint8_t*, recomp_context* c) {
    in_frame(c); events.push_back(3); clobber(c);
    if (throw_on_friction) throw std::runtime_error("Injected helper failure");
    c->f0.fl = .5f;
}
extern "C" void resident_wave9_func_002B7060(std::uint8_t*, recomp_context* c) {
    in_frame(c); events.push_back(4); clobber(c); c->f0.fl = 1.0f;
}
extern "C" void resident_wave14_func_002B6490(std::uint8_t*, recomp_context* c) {
    in_frame(c); events.push_back(5); clobber(c); c->f0.fl = .25f;
}
extern "C" void trace_func_00272738_r00073338(std::uint8_t*, recomp_context* c) {
    in_frame(c); need(static_cast<std::uint32_t>(c->r4) == slot && number(slot + 4) == 0.0f);
    need(static_cast<std::uint32_t>(c->r5) == std::bit_cast<std::uint32_t>(2.0f)); // forward max (params+4)
    events.push_back(6); scalar(slot, number(slot) * .5f); clobber(c);
}
extern "C" void resident_wave14_func_00271F8C(std::uint8_t*, recomp_context* c) {
    in_frame(c); need(static_cast<std::uint32_t>(c->r4) == slot && static_cast<std::uint32_t>(c->r5) == slot);
    need(static_cast<std::uint32_t>(c->r6) == std::bit_cast<std::uint32_t>(2.0f));
    for (unsigned i = 0; i < 3; ++i) { scalar(slot + 4 * i, number(slot + 4 * i) * 2.0f); last_scaled[i] = number(slot + 4 * i); }
    events.push_back(7); clobber(c);
}
extern "C" void resident_wave14_func_00271F24(std::uint8_t*, recomp_context* c) {
    in_frame(c); need(static_cast<std::uint32_t>(c->r4) == actor + 0x6E8 && static_cast<std::uint32_t>(c->r5) == actor + 0x6E8);
    need(static_cast<std::uint32_t>(c->r6) == slot);
    for (unsigned i = 0; i < 3; ++i) { need(number(slot + 4 * i) == last_scaled[i]); scalar(actor + 0x6E8 + 4 * i, number(actor + 0x6E8 + 4 * i) + number(slot + 4 * i)); }
    events.push_back(8); clobber(c);
}
extern "C" void trace_func_0025DDF4_r0005E9F4(std::uint8_t*, recomp_context* c) {
    in_frame(c); need(static_cast<std::int64_t>(c->r4) == -16 && get(frame + 0x10) == 0 && get(frame + 0x14) == 0xFFFFFFFFU);
    need(static_cast<std::uint32_t>(c->r5) == get(actor + 8) && static_cast<std::uint32_t>(c->r7) == get(actor + 16));
    events.push_back(9); clobber(c);
}
extern "C" int xr64_xr_locomotion_stick(float*, float*, float*) { return 0; } // No VR in this contract.
extern "C" recomp_func_t* get_function_with_context(std::uint8_t*, recomp_context*, int32_t) {
    throw std::runtime_error("Unexpected indirect dependency");
}

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char**) {
    const bool expected_enabled = argc > 1;
    { // VR analog direction: signs from raw digital agreement, then yaw rotation.
        float f = 9, s = 9;
        REQUIRE(xr64_ground_movement_analog(0, 1, 0, 0, 0, &f, &s) == 0 && f == 9);
        REQUIRE(xr64_ground_movement_analog(0, 1, 0, 1, 0, &f, &s) == 0);   // forward learned only
        REQUIRE(xr64_ground_movement_analog(1, 0, 0, 0, -1, &f, &s) == 1);  // strafe inverted
        REQUIRE(f == 0 && s == -1);
        REQUIRE(xr64_ground_movement_analog(0, .05f, 0, 0, 0, &f, &s) == 1 && f == 0 && s == 0); // deadzone
        // Reference turned 90 degrees left: stick forward moves body-left.
        REQUIRE(xr64_ground_movement_analog(0, 1, 1.5707964f, 0, 0, &f, &s) == 1);
        REQUIRE(std::fabs(f) < 1e-6f && std::fabs(s - 1) < 1e-6f);
        // Diagonal keeps unit length after the deadzone remap.
        REQUIRE(xr64_ground_movement_analog(.7071068f, .7071068f, 0, 0, 0, &f, &s) == 1);
        REQUIRE(std::fabs(f * f + s * s - 1) < 1e-5f);
    }
    seed(); auto c = context(); const auto saved = c; const auto before = words;
    const int handled = xr64_ground_movement_try_update(words.data(), &c);
    REQUIRE(std::memcmp(&c, &saved, sizeof(c)) == 0);
    if (!expected_enabled) {
        REQUIRE(handled == 0 && events.empty() && words == before);
        std::puts("PASS default preserves original route/context/storage"); return 0;
    }
    // state, forward accelerate, rotate, strafe friction, rotate, clamp, forward
    // impulse (no friction), rotate, lateral friction, rotate, scale, add, sound.
    REQUIRE(handled == 1 && events == std::vector<unsigned>({1, 2, 4, 5, 3, 4, 5, 6, 4, 5, 3, 4, 5, 7, 8, 9}));
    REQUIRE(number(actor + 0x6C0) == 3.0f && number(actor + 0x6C4) == .5f);
    REQUIRE(number(actor + 0x6D4) == 7.0f && number(actor + 0x6D8) == .5f); // params+0x18 = 7
    REQUIRE(number(actor + 0x6DC) == .5f);                                  // 0 + 800C280C
    REQUIRE(number(actor + 0x6E8) == 100.0f + last_scaled[0] && number(actor + 0x6F0) == 300.0f + last_scaled[2]);
    const auto expect_fallback = [&](auto modify) {
        seed(); auto ctx = context(); modify(ctx); const auto prior = ctx; const auto image = words;
        return xr64_ground_movement_try_update(words.data(), &ctx) == 0 && events.empty()
            && std::memcmp(&ctx, &prior, sizeof(ctx)) == 0 && words == image;
    };
    REQUIRE(expect_fallback([](auto& x) { x.mips3_float_mode = 0; x.f_odd = &x.f0.u32h; }));
    REQUIRE(expect_fallback([](auto& x) { x.r6 = (gpr)(int32_t)0x800C9404; }));
    REQUIRE(expect_fallback([](auto& x) { x.r4 = 0; }));
    REQUIRE(expect_fallback([](auto& x) { x.r5 = 0; }));
    seed(); c = context(); const auto before_throw = c; throw_on_friction = true;
    bool threw = false;
    try { (void)xr64_ground_movement_try_update(words.data(), &c); }
    catch (const std::runtime_error&) { threw = true; }
    REQUIRE(threw && events == std::vector<unsigned>({1, 2, 4, 5, 3}));
    REQUIRE(std::memcmp(&c, &before_throw, sizeof(c)) == 0);
    REQUIRE(number(actor + 0x6C0) == 3.0f); // Propagated failure, no original replay.
    std::puts("PASS translated storage, helper frame/ABI/order, staged vector, sound stack args, context restore, four fallback guards, exception propagation");
    return 0;
}
