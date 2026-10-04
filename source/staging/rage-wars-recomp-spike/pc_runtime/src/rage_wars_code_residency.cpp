#include "rage_wars_code_residency.hpp"

namespace rage_wars {
namespace {

thread_local const RageWarsCodeResidency *active_service = nullptr;

class ActiveRequestGuard {
public:
    explicit ActiveRequestGuard(const RageWarsCodeResidency *service)
        : previous_(active_service) {
        active_service = service;
    }

    ~ActiveRequestGuard() {
        active_service = previous_;
    }

private:
    const RageWarsCodeResidency *previous_;
};

} // namespace

RageWarsCodeResidency::RageWarsCodeResidency(
        CodeResidencyDependencies dependencies)
    : dependencies_(dependencies) {
}

CodeResidencyResult RageWarsCodeResidency::resolve(
        std::uint8_t *rdram, recomp_context *ctx, std::int32_t vram) const {
    if (dependencies_.find_resident != nullptr) {
        if (recomp_func_t *function = dependencies_.find_resident(vram)) {
            return {CodeResidencyStatus::resident, function};
        }
    }

    if (dependencies_.find_native_resident != nullptr) {
        if (recomp_func_t *function = dependencies_.find_native_resident(vram)) {
            return {CodeResidencyStatus::native_resident, function};
        }
    }

    recomp_func_t *declared_function = dependencies_.find_declared != nullptr
            ? dependencies_.find_declared(vram) : nullptr;
    const auto validate_declared = [&]() {
        return declared_function != nullptr && rdram != nullptr &&
                dependencies_.validate_declared_entry != nullptr &&
                dependencies_.validate_declared_entry(
                        rdram, vram, declared_function);
    };
    if (validate_declared()) {
        return {CodeResidencyStatus::native_unit_available, declared_function};
    }

    const std::uint32_t guest_vram = static_cast<std::uint32_t>(vram);
    if (guest_vram >= 0x80000000U) {
        return {declared_function != nullptr
                    ? CodeResidencyStatus::unavailable_after_load
                    : CodeResidencyStatus::unknown_unit,
                nullptr};
    }

    if (active_service == this) {
        return {CodeResidencyStatus::reentrant_request, nullptr};
    }

    if (rdram == nullptr || ctx == nullptr ||
            dependencies_.request_availability == nullptr) {
        return {declared_function != nullptr
                    ? CodeResidencyStatus::load_declined
                    : CodeResidencyStatus::unknown_unit,
                nullptr};
    }

    const ActiveRequestGuard guard(this);
    if (!dependencies_.request_availability(rdram, ctx, guest_vram)) {
        return {declared_function != nullptr
                    ? CodeResidencyStatus::load_declined
                    : CodeResidencyStatus::unknown_unit,
                nullptr};
    }

    if (dependencies_.find_resident != nullptr) {
        if (recomp_func_t *function = dependencies_.find_resident(vram)) {
            return {CodeResidencyStatus::made_resident, function};
        }
    }

    if (validate_declared()) {
        return {CodeResidencyStatus::native_unit_available, declared_function};
    }

    return {CodeResidencyStatus::unavailable_after_load, nullptr};
}


const char *code_residency_status_name(CodeResidencyStatus status) {
    switch (status) {
        case CodeResidencyStatus::resident: return "resident";
        case CodeResidencyStatus::made_resident: return "made-resident";
        case CodeResidencyStatus::native_resident: return "native-resident";
        case CodeResidencyStatus::native_unit_available: return "native-unit-available";
        case CodeResidencyStatus::unknown_unit: return "unknown-unit";
        case CodeResidencyStatus::load_declined: return "load-declined";
        case CodeResidencyStatus::unavailable_after_load: return "unavailable-after-load";
        case CodeResidencyStatus::reentrant_request: return "reentrant-request";
    }
    return "invalid-status";
}
} // namespace rage_wars
