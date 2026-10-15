#include "rage_wars_player_view_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "funcs.h"
#include "player_view.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {
using namespace rage_wars::player;
gpr argument(std::uint32_t word) { return static_cast<gpr>(static_cast<std::int32_t>(word)); }

class CompatibilityWords final : public WordStorage {
public:
    explicit CompatibilityWords(std::uint8_t* memory) : memory_(memory) {}
    std::uint32_t load_word(ObjectId object, std::uint32_t offset) const override {
        std::uint32_t value;
        std::memcpy(&value, resolve(object, offset), sizeof(value));
        return value;
    }
    void store_word(ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        std::memcpy(resolve(object, offset), &value, sizeof(value));
    }
private:
    std::uint8_t* resolve(ObjectId object, std::uint32_t offset) const {
        const std::uint32_t address32 = object + offset;
        if ((address32 & 3U) != 0) throw std::runtime_error("Unaligned PlayerView word field");
        const auto address = static_cast<gpr>(static_cast<std::int32_t>(address32));
        // Reuse the active compatibility translator, including non-KSEG aliases.
        auto* first = xr64_recomp_resolve_address(memory_, address);
        auto* last = xr64_recomp_resolve_address(memory_,
            static_cast<gpr>(static_cast<std::int32_t>(address32 + 3U)));
        if (last != first + 3) throw std::runtime_error("Noncontiguous PlayerView word field");
        return first;
    }
    std::uint8_t* memory_;
};

class CompatibilityGlobals final : public ViewGlobals {
public:
    explicit CompatibilityGlobals(CompatibilityWords& storage) : storage_(storage) {}
    std::uint32_t load_word(ViewGlobal value) const override {
        return storage_.load_word(address(value), 0);
    }
    void store_word(ViewGlobal value, std::uint32_t word) override {
        storage_.store_word(address(value), 0, word);
    }
private:
    static ObjectId address(ViewGlobal value) {
        static constexpr std::array<ObjectId, 14> addresses{
            0x800C26D4, 0x800C26D8, 0x800C26DC, 0x800C26E0,
            0x800C26E4, 0x800C26E8, 0x800C26EC, 0x800C26F0,
            0x800C3450, 0x800C3454, 0x800C3458, 0x800C9EB4,
            0x800C9EB8, 0x800CD7E0};
        return addresses.at(static_cast<std::size_t>(value));
    }
    CompatibilityWords& storage_;
};

// The adapter supplies the qualified guest helper ABI. The native contract does
// not own CPU registers, stack translation, scheduling, or these dependencies.
class CompatibilityCallbacks final : public ViewCallbacks {
public:
    CompatibilityCallbacks(std::uint8_t* memory, recomp_context& context)
        : memory_(memory), context_(context), incoming_sp_(context.r29) {}
    bool state_query(StateQuery) override {
        commit_frame();
        resident_wave9_func_00245778(memory_, &context_);
        return context_.r2 != 0;
    }
    ObjectId locate_region(ObjectId camera) override {
        commit_frame();
        context_.r4 = argument(0x80119DC8);
        context_.r5 = argument(camera + 0x38);
        resident_wave14_func_00286678(memory_, &context_);
        return static_cast<ObjectId>(context_.r2);
    }
    float region_height(ObjectId region, float x, float z) override {
        commit_frame();
        context_.r4 = argument(region);
        context_.r5 = argument(std::bit_cast<std::uint32_t>(x));
        context_.r6 = argument(std::bit_cast<std::uint32_t>(z));
        get_function_with_context(memory_, &context_, 0x00275DC4)(memory_, &context_);
        return context_.f0.fl;
    }
    float first_angle_function(float value) override {
        producer_frame();
        context_.f12.fl = value;
        resident_wave9_func_002B7060(memory_, &context_);
        return context_.f0.fl;
    }
    float second_angle_function(float value) override {
        producer_frame();
        context_.f12.fl = value;
        resident_wave14_func_002B6490(memory_, &context_);
        return context_.f0.fl;
    }
    void update_camera(ObjectId camera) override {
        producer_frame();
        context_.r4 = argument(camera);
        resident_wave14_func_00234FCC(memory_, &context_);
    }
    void update_actor(ObjectId actor) override {
        producer_frame();
        context_.r4 = argument(actor);
        trace_func_0021D750_r0001E350(memory_, &context_);
    }
private:
    void producer_frame() { context_.r29 = ADD32(incoming_sp_, -0x68); }
    void commit_frame() { context_.r29 = ADD32(incoming_sp_, -0xA8); }
    std::uint8_t* memory_;
    recomp_context& context_;
    gpr incoming_sp_;
};

struct PreserveContext {
    recomp_context& context;
    recomp_context saved;
    ~PreserveContext() { context = saved; }
};

bool enabled() {
    // On by default; XR64_NATIVE_PLAYER_VIEW=0 switches it off.
    static const bool choice = xr64::rage_wars::recomp::feature_default_on("XR64_NATIVE_PLAYER_VIEW");
    return choice;
}
} // namespace

extern "C" int xr64_player_view_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || context == nullptr || memory == nullptr) return 0;
    const auto actor = static_cast<ObjectId>(context->r4);
    const auto source = static_cast<ObjectId>(context->r5);
    // Observed aliasing and attached mode1 bound this experiment. Other camera
    // modes/providers retain their original bodies and selection semantics.
    if (actor != source || !context->mips3_float_mode) return 0;
    CompatibilityWords words(memory);
    if (words.load_word(actor, 0x5D0) != 1
            || words.load_word(actor, 0x5DC) == 0
            || words.load_word(actor, 0x1450) != 0) return 0;

    PreserveContext restore{*context, *context};
    RageWarsActorStorage storage(words);
    CompatibilityGlobals globals(words);
    CompatibilityCallbacks callbacks(memory, *context);
    PlayerView(storage, globals, callbacks).update(actor, source);
    // One status witness lets a protected gameplay comparison distinguish an
    // executed native path from a guard fallback. It contains no captured state.
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    if (!reported.test_and_set(std::memory_order_relaxed)) {
        std::fprintf(stderr, "PLAYER_VIEW_NATIVE_ATTACHED_UPDATE_COMPLETED\n");
    }
    return 1;
}