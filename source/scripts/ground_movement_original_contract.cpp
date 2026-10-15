#include "ground_movement.hpp"
#include "recomp.h"
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <xmmintrin.h>
#pragma fenv_access(on)

using namespace rage_wars::player;
namespace {
constexpr std::uint32_t Actor = 0x80100000, View = 0x80102000, Params = 0x800C9400;
constexpr std::uint32_t Stack = 0x807F0000;
// MovementConstant order: 800C27E4..800C280C step 4, then frame time 800CD648.
constexpr std::array<std::uint32_t, 12> ConstantAddresses{
    0x800C27E4, 0x800C27E8, 0x800C27EC, 0x800C27F0, 0x800C27F4, 0x800C27F8,
    0x800C27FC, 0x800C2800, 0x800C2804, 0x800C2808, 0x800C280C, 0x800CD648};
std::uint32_t bits(float x) { return std::bit_cast<std::uint32_t>(x); }
float number(std::uint32_t x) { return std::bit_cast<float>(x); }
gpr guest(std::uint32_t x) { return static_cast<gpr>(static_cast<std::int32_t>(x)); }
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
float add(float a, float b) { volatile float r = a + b; return r; }
float mul(float a, float b) { volatile float r = a * b; return r; }
struct Event {
    std::string tag;
    std::vector<std::uint32_t> words;
    bool operator==(const Event&) const = default;
};
struct Mutation { std::string stage; std::uint32_t address, value; };
struct Plan {
    bool blocked = false;
    std::vector<Mutation> mutations;
    std::vector<std::pair<std::string, int>> rounding;
};
class World final : public WordStorage, public MovementConstants, public MovementCallbacks {
public:
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x800000);
    Plan plan;
    std::vector<Event> trace;
    std::uint32_t actor = Actor, view = Actor;
    bool tracking = false;
    bool defer_native_store = false, deferred = false;
    std::uint32_t deferred_address = 0, deferred_value = 0;
    std::uint32_t read(std::uint32_t address) const {
        const auto offset = address & 0x7FFFFF;
        require(offset + 4 <= ram.size() && !(offset & 3), "invalid aligned guest read");
        std::uint32_t v; std::memcpy(&v, ram.data() + offset, 4); return v;
    }
    static bool stack(std::uint32_t address) {
        const auto o = address & 0x7FFFFF, s = Stack & 0x7FFFFF;
        return o >= s - 0x200 && o < s + 0x100;
    }
    void put(std::uint32_t address, std::uint32_t value) {
        const auto offset = address & 0x7FFFFF;
        require(offset + 4 <= ram.size() && !(offset & 3), "invalid aligned guest write");
        if (tracking && !stack(address)) trace.push_back({"store", {address, value}});
        std::memcpy(ram.data() + offset, &value, 4);
    }
    void scalar_at(std::uint32_t address, float x) { put(address, bits(x)); }
    // WordStorage (native side). The negative control delays the first forward
    // speed store past the next callback; final memory is unchanged.
    std::uint32_t load_word(ObjectId id, std::uint32_t offset) const override {
        require(id != 0, "native null object field read"); return read(id + offset);
    }
    void store_word(ObjectId id, std::uint32_t offset, std::uint32_t x) override {
        if (defer_native_store && !deferred && id == actor && offset == 0x6C0) {
            deferred = true; deferred_address = id + offset; deferred_value = x; return;
        }
        put(id + offset, x);
    }
    float scalar(MovementConstant c) const override {
        return number(read(ConstantAddresses[static_cast<unsigned>(c)]));
    }
    void callback(const std::string& tag, std::vector<std::uint32_t> args) {
        args.push_back(static_cast<std::uint32_t>(std::fegetround()));
        for (unsigned o = 0x6A4; o <= 0x6F0; o += 4) args.push_back(read(actor + o));
        args.push_back(read(actor + 0x7E8));
        args.push_back(read(view + 0x6C));
        for (auto a : ConstantAddresses) args.push_back(read(a));
        trace.push_back({tag, std::move(args)});
        if (deferred) { deferred = false; defer_native_store = false; put(deferred_address, deferred_value); }
        for (const auto& m : plan.mutations) if (m.stage == tag) put(m.address, m.value);
        for (const auto& r : plan.rounding) if (r.first == tag) std::fesetround(r.second);
    }
    // Deterministic mocks shared by both sides.
    bool game_state_blocks() override { callback("state", {plan.blocked}); return plan.blocked; }
    float accelerate(float c, float a, float l) override {
        callback("accelerate", {bits(c), bits(a), bits(l)}); return add(c, mul(add(a, l), 0.375f));
    }
    float friction(float c, float r) override {
        callback("friction", {bits(c), bits(r)}); return mul(add(c, r), 0.625f);
    }
    float first_angle_function(float a) override { callback("first", {bits(a)}); return add(mul(a, 0.5f), 0.125f); }
    float second_angle_function(float a) override { callback("second", {bits(a)}); return add(mul(a, -0.25f), 0.75f); }
    void clamp_length(MotionVector& v, float m) override {
        callback("clamp", {bits(v.x), bits(v.y), bits(v.z), bits(m)});
        v.x = mul(v.x, 0.5f); v.y = add(v.y, 0.25f); v.z = mul(v.z, m);
    }
    void scale(MotionVector& v, float s) override {
        callback("scale", {bits(v.x), bits(v.y), bits(v.z), bits(s)});
        v.x = mul(v.x, s); v.y = mul(v.y, s); v.z = mul(v.z, s);
    }
    void add_to_proposed(ObjectId id, const MotionVector& v) override {
        callback("add", {id, bits(v.x), bits(v.y), bits(v.z)});
        const auto base = id + 0x6E8;
        put(base, bits(add(number(read(base)), v.x)));
        put(base + 4, bits(add(number(read(base + 4)), v.y)));
        put(base + 8, bits(add(number(read(base + 8)), v.z)));
    }
    void impulse_sound(std::int16_t s, std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        callback("sound", {static_cast<std::uint32_t>(static_cast<std::int32_t>(s)), x, y, z, 0, 0xFFFFFFFF});
    }
};
thread_local World* Active = nullptr;
struct OriginalWord {
    std::uint32_t address;
    operator std::int32_t() const { return static_cast<std::int32_t>(Active->read(address)); }
    OriginalWord& operator=(std::uint32_t value) { Active->put(address, value); return *this; }
};
OriginalWord original_word(std::uint8_t* ram, gpr address) {
    require(Active && Active->ram.data() == ram, "oracle memory owner mismatch");
    return {static_cast<std::uint32_t>(address)};
}
void poison(recomp_context* ctx) {
    // Original bodies must not depend on caller-saved state across calls.
    gpr* regs = &ctx->r0;
    for (unsigned r = 1; r <= 15; ++r) regs[r] = guest(0xC1000000 + r);
    regs[24] = guest(0xC1000018); regs[25] = guest(0xC1000019);
    auto* fp = &ctx->f0;
    for (unsigned r = 0; r < 20; ++r) fp[r].u64 = 0x3FF0000000000000ULL + r;
}
MotionVector vector_at(std::uint32_t a) {
    return {number(Active->read(a)), number(Active->read(a + 4)), number(Active->read(a + 8))};
}
void vector_to(std::uint32_t a, const MotionVector& v) {
    Active->put(a, bits(v.x)); Active->put(a + 4, bits(v.y)); Active->put(a + 8, bits(v.z));
}
std::uint32_t r(gpr value) { return static_cast<std::uint32_t>(value); }
}

// The pinned original body is included byte-for-byte; only MEM_W is routed
// through the recorder. Its direct dependencies are the guest-ABI mocks below.
#undef MEM_W
#define MEM_W(offset, reg) original_word(rdram, (reg) + (offset))
void trace_func_002233CC_r00023FCC(std::uint8_t*, recomp_context*);
void resident_wave9_func_00245764(std::uint8_t*, recomp_context* ctx) {
    const bool b = Active->game_state_blocks(); poison(ctx); ctx->r2 = b ? 1 : 0;
}
void trace_func_00274690_r00075290(std::uint8_t*, recomp_context* ctx) {
    const float v = Active->accelerate(ctx->f12.fl, ctx->f14.fl, number(r(ctx->r6)));
    poison(ctx); ctx->f0.u32l = bits(v);
}
void resident_wave14_func_00274790(std::uint8_t*, recomp_context* ctx) {
    const float v = Active->friction(ctx->f12.fl, ctx->f14.fl); poison(ctx); ctx->f0.u32l = bits(v);
}
void resident_wave9_func_002B7060(std::uint8_t*, recomp_context* ctx) {
    const float v = Active->first_angle_function(ctx->f12.fl); poison(ctx); ctx->f0.u32l = bits(v);
}
void resident_wave14_func_002B6490(std::uint8_t*, recomp_context* ctx) {
    const float v = Active->second_angle_function(ctx->f12.fl); poison(ctx); ctx->f0.u32l = bits(v);
}
void trace_func_00272738_r00073338(std::uint8_t*, recomp_context* ctx) {
    const auto at = r(ctx->r4); require(World::stack(at), "clamp vector not on guest frame");
    auto v = vector_at(at); Active->clamp_length(v, number(r(ctx->r5))); vector_to(at, v); poison(ctx);
}
void resident_wave14_func_00271F8C(std::uint8_t*, recomp_context* ctx) {
    require(r(ctx->r4) == r(ctx->r5) && World::stack(r(ctx->r4)), "scale is not in-place on frame vector");
    auto v = vector_at(r(ctx->r5)); Active->scale(v, number(r(ctx->r6))); vector_to(r(ctx->r4), v); poison(ctx);
}
void resident_wave14_func_00271F24(std::uint8_t*, recomp_context* ctx) {
    require(r(ctx->r4) == r(ctx->r5), "proposed add is not in-place");
    Active->add_to_proposed(r(ctx->r4) - 0x6E8, vector_at(r(ctx->r6))); poison(ctx);
}
void trace_func_0025DDF4_r0005E9F4(std::uint8_t*, recomp_context* ctx) {
    const auto sp = r(ctx->r29);
    require(Active->read(sp + 0x10) == 0 && Active->read(sp + 0x14) == 0xFFFFFFFF, "sound stack arguments");
    require(static_cast<std::int64_t>(ctx->r4) == static_cast<std::int16_t>(r(ctx->r4)), "sound id not sign-extended");
    Active->impulse_sound(static_cast<std::int16_t>(r(ctx->r4)), r(ctx->r5), r(ctx->r6), r(ctx->r7));
    poison(ctx);
}
extern "C" recomp_func_t* get_function_with_context(std::uint8_t*, recomp_context*, std::int32_t) {
    throw std::runtime_error("unexpected indirect dependency");
}
#include "generated_original.inc"

namespace {
World baseline(bool distinct_view = false) {
    World w; w.view = distinct_view ? View : Actor;
    for (auto id : {Actor, View}) { w.scalar_at(id + 0x08, 3.f); w.scalar_at(id + 0x0C, 10.f); w.scalar_at(id + 0x10, -4.f); w.scalar_at(id + 0x6C, 1.25f); }
    w.put(Actor + 0x11B8, 0);
    for (auto [o, v] : std::array<std::pair<unsigned, float>, 12>{{
            {0x6A4, .5f}, {0x6A8, -.75f}, {0x6C0, 1.5f}, {0x6C4, -.25f}, {0x6D4, .375f}, {0x6D8, -.5f},
            {0x6DC, 0.f}, {0x66C, .125f}, {0x6E8, 100.f}, {0x6EC, 20.f}, {0x6F0, -30.f}, {0x6C8, 9.f}}})
        w.scalar_at(Actor + o, v);
    w.put(Actor + 0x6B0, 0); w.put(Actor + 0x6B8, 0); w.put(Actor + 0x7E8, 0);
    std::array<float, 8> p{2.f, 4.f, .5f, 1.5f, 3.f, .25f, 6.f, .75f};
    for (unsigned i = 0; i < p.size(); ++i) w.scalar_at(Params + i * 4, p[i]);
    w.put(Params + 0x20, 0x1234FFE5); // halfword +0x22 = 0xFFE5 (negative id)
    std::array<float, 12> c{1.25f, .5f, 1.5f, .25f, .875f, -.125f, 1.125f, .625f, 2.f, .375f, .0625f, 1.f / 30.f};
    for (unsigned i = 0; i < c.size(); ++i) w.scalar_at(ConstantAddresses[i], c[i]);
    return w;
}
void original_run(World& w) {
    recomp_context ctx{}; ctx.mips3_float_mode = 1; ctx.f_odd = &ctx.f1.u32l;
    ctx.r29 = guest(Stack); ctx.r31 = guest(0x81234564);
    ctx.r16 = guest(0x91110010); ctx.r17 = guest(0x92220020); ctx.r18 = guest(0x93330030);
    ctx.r19 = guest(0x94440040); ctx.r20 = guest(0x95550050);
    ctx.f20.u64 = 0x3FF2345600000020ULL; ctx.f21.u64 = 0x3FF3456700000021ULL;
    const auto saved = ctx;
    ctx.r4 = guest(w.actor); ctx.r5 = guest(w.view); ctx.r6 = guest(Params);
    w.tracking = true; Active = &w;
    trace_func_002233CC_r00023FCC(w.ram.data(), &ctx);
    Active = nullptr;
    require(ctx.r29 == saved.r29 && ctx.r31 == saved.r31 && ctx.r16 == saved.r16 && ctx.r17 == saved.r17
            && ctx.r18 == saved.r18 && ctx.r19 == saved.r19 && ctx.r20 == saved.r20, "original GPR/frame ABI failed");
    require(ctx.f20.u64 == saved.f20.u64 && ctx.f21.u64 == saved.f21.u64, "original saved FP ABI failed");
}
unsigned Passed = 0;
bool NegativeControl = false;
void run_case(const std::string& name, const World& initial) {
    World original = initial, native = initial;
    std::fenv_t entry; std::fegetenv(&entry);
    original_run(original);
    const auto original_rounding = std::fegetround(); std::fesetenv(&entry);
    native.tracking = true; native.defer_native_store = NegativeControl;
    RageWarsActorStorage storage(native);
    GroundMovement(storage, native, native).update(native.actor, native.view, Params);
    require(std::fegetround() == original_rounding, "callback rounding environment mismatch"); std::fesetenv(&entry);
    if (original.trace != native.trace) {
        unsigned at = 0; while (at < original.trace.size() && at < native.trace.size() && original.trace[at] == native.trace[at]) ++at;
        std::fprintf(stderr, "%s trace mismatch at %u: original=%s native=%s\n", name.c_str(), at,
            at < original.trace.size() ? original.trace[at].tag.c_str() : "END", at < native.trace.size() ? native.trace[at].tag.c_str() : "END");
        if (at < original.trace.size() && at < native.trace.size())
            for (unsigned i = 0; i < std::min(original.trace[at].words.size(), native.trace[at].words.size()); ++i)
                if (original.trace[at].words[i] != native.trace[at].words[i])
                    std::fprintf(stderr, "  word%u original=%08X native=%08X\n", i, original.trace[at].words[i], native.trace[at].words[i]);
        throw std::runtime_error(name + " callback/store sequence mismatch");
    }
    for (unsigned o = 0; o < 0x800000; o += 4) {
        if (World::stack(0x80000000 + o)) continue;
        const auto a = original.read(0x80000000 + o), b = native.read(0x80000000 + o);
        if (a != b) { std::fprintf(stderr, "%s memory mismatch %08X: original=%08X native=%08X\n", name.c_str(), 0x80000000 + o, a, b); throw std::runtime_error(name + " exact output mismatch"); }
    }
    ++Passed; std::printf("PASS %s (%zu events)\n", name.c_str(), original.trace.size());
}
void effect(World& w, const char* stage, std::uint32_t a, std::uint32_t v) { w.plan.mutations.push_back({stage, a, v}); }
void named_cases() {
    auto w = baseline(); run_case("baseline_shared_view", w);
    w = baseline(true); run_case("distinct_view", w);
    w = baseline(); w.plan.blocked = true; run_case("game_state_blocks", w);
    for (std::uint32_t freeze : {1u, 0x80000000u}) { w = baseline(); w.put(Actor + 0x11B8, freeze); run_case("freeze_word_" + std::to_string(freeze), w); }
    for (float cd : {.5f, 0.f, -0.f, -1.f}) { w = baseline(); w.scalar_at(Actor + 0x6DC, cd); w.put(Actor + 0x6B0, 8); run_case("cooldown_" + std::to_string(bits(cd)), w); }
    w = baseline(); w.put(Actor + 0x6DC, 0x7FC12345); w.put(Actor + 0x6B0, 8); run_case("cooldown_unordered", w);
    for (float s : {5.f, -5.f, 4.f, -4.f, std::nextafter(4.f, INFINITY), -0.f}) { w = baseline(); w.scalar_at(Actor + 0x6C0, s); run_case("forward_speed_" + std::to_string(bits(s)), w); }
    for (float s : {3.5f, -3.5f, 3.f, -3.f}) { w = baseline(); w.scalar_at(Actor + 0x6C4, s); run_case("strafe_speed_" + std::to_string(bits(s)), w); }
    for (float in : {0.f, -0.f, 1.f, -1.f}) { w = baseline(); w.scalar_at(Actor + 0x6A8, in); run_case("forward_input_" + std::to_string(bits(in)), w); w.scalar_at(Actor + 0x6A4, in); run_case("both_inputs_" + std::to_string(bits(in)), w); }
    w = baseline(); w.put(Actor + 0x6A8, 0x7FC12345); run_case("forward_input_unordered", w);
    w = baseline(); w.put(Actor + 0x6C0, 0x7FC12345); run_case("forward_speed_unordered", w);
    for (std::uint32_t z : {1u, 2u, 3u}) { w = baseline(); w.put(Actor + 0x7E8, z); run_case("zoom_request_" + std::to_string(z), w); }
    for (std::uint32_t b = 0; b < 16; ++b) for (std::uint32_t e = 0; e < 4; ++e) {
        w = baseline(); w.put(Actor + 0x6B0, b << 2); w.put(Actor + 0x6B8, e); run_case("impulse_bits_" + std::to_string(b << 2) + "_" + std::to_string(e), w);
    }
    w = baseline(); w.scalar_at(Actor + 0x6DC, .25f); w.put(Actor + 0x6B0, 0xC); w.put(Actor + 0x6B8, 3); run_case("impulse_bits_ignored_during_cooldown", w);
    // Callback mutations: reloads after calls, and $f20 offsets not reloaded.
    w = baseline(); effect(w, "first", Actor + 0x6C, bits(7.f)); effect(w, "first", Actor + 0x6C0, bits(-2.f)); effect(w, "first", ConstantAddresses[1], bits(9.f)); run_case("first_mutates_yaw_speed_offset", w);
    w = baseline(true); effect(w, "second", View + 0x6C, bits(-3.f)); effect(w, "second", Actor + 0x6C4, bits(8.f)); effect(w, "second", ConstantAddresses[3], bits(4.f)); run_case("second_mutates_distinct_view", w);
    w = baseline(); effect(w, "accelerate", Actor + 0x6C0, bits(55.f)); effect(w, "accelerate", Actor + 0x6A4, 0); effect(w, "accelerate", Params + 0x10, bits(.125f)); run_case("accelerate_result_overwrites_mutation", w);
    w = baseline(); w.put(Actor + 0x6B0, 8); effect(w, "friction", Actor + 0x6B8, 1); effect(w, "friction", Actor + 0x6DC, bits(3.f)); effect(w, "clamp", Actor + 0x66C, bits(2.f)); effect(w, "scale", Actor + 0x6E8, bits(-1.f)); run_case("mid_sequence_mutations", w);
    w = baseline(); w.put(Actor + 0x6B0, 4); effect(w, "add", Params + 0x20, 0x00000007); effect(w, "add", Actor + 0x08, bits(42.f)); effect(w, "add", ConstantAddresses[10], bits(5.f)); run_case("add_mutates_sound_inputs", w);
    w = baseline(); w.put(Actor + 0x6B0, 8); w.put(Params + 0x20, 0xFFFF7FFF); run_case("sound_positive_id", w);
    w = baseline(); w.plan.rounding = {{"first", FE_UPWARD}, {"friction", FE_DOWNWARD}, {"clamp", FE_TOWARDZERO}}; run_case("callbacks_change_rounding", w);
    w = baseline(); w.put(Actor + 0x6C, 0x80000000); w.put(Actor + 0x6C0, 0x80000000); run_case("negative_zero_yaw_speed", w);
    w = baseline(); w.put(Actor + 0x6A4, 0x00000001); w.put(Actor + 0x66C, 0x00800000); run_case("subnormal_input_and_scale", w);
    w = baseline(); w.scalar_at(Actor + 0x6C0, 16777216.f); w.scalar_at(Params + 4, 16777215.f); run_case("binary32_magnitude_boundary", w);
}
void random_cases() {
    std::uint32_t state = 0x2468ACE1;
    auto next = [&] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
    auto f = [&](int range) { return static_cast<float>(static_cast<int>(next() % (2 * range + 1)) - range) / 16.f; };
    for (unsigned i = 0; i < 64; ++i) {
        auto w = baseline((i & 1) != 0);
        for (auto o : {0x6A4, 0x6A8, 0x6C0, 0x6C4, 0x6D4, 0x6D8, 0x6DC, 0x66C, 0x6E8, 0x6EC, 0x6F0}) w.scalar_at(Actor + o, f(96));
        for (unsigned k = 0; k < 8; ++k) w.scalar_at(Params + k * 4, f(64));
        w.scalar_at(w.view + 0x6C, f(100));
        w.put(Actor + 0x6B0, next() & 0xC); w.put(Actor + 0x6B8, next() & 3); w.put(Actor + 0x7E8, next() % 3);
        if ((next() & 7) == 0) w.scalar_at(Actor + 0x6A8, 0.f);
        run_case("seeded_finite_" + std::to_string(i), w);
    }
}
}
int main(int argc, char** argv) {
    const auto saved_mxcsr = _mm_getcsr(); std::fenv_t saved; std::fegetenv(&saved);
    std::fesetenv(FE_DFL_ENV); std::fesetround(FE_TONEAREST); _mm_setcsr(_mm_getcsr() & ~(0x8000u | 0x40u));
    try {
        NegativeControl = argc == 2 && std::string(argv[1]) == "--negative-control";
        require(argc == 1 || NegativeControl, "usage: [--negative-control]");
        named_cases(); random_cases();
        for (auto round : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
            std::fesetround(round); auto w = baseline(); w.put(Actor + 0x6B0, 8); run_case("host_rounding_" + std::to_string(round), w);
        }
        std::fesetround(FE_TONEAREST);
        std::printf("TOTAL_PASS %u; SYNTHETIC_ORIGINAL_BODY_DIFFERENTIAL; NDEBUG_RELEASE_ORACLE; STRICT_FP\n", Passed);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL %s\n", e.what()); std::fesetenv(&saved); _mm_setcsr(saved_mxcsr); return 1;
    }
    std::fesetenv(&saved); _mm_setcsr(saved_mxcsr); return 0;
}
