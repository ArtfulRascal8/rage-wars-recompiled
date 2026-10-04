#pragma once

#include "recomp.h"

#include <cstdint>

namespace rage_wars {

enum class CodeResidencyStatus {
    resident,
    made_resident,
    native_resident,
    native_unit_available,
    unknown_unit,
    load_declined,
    unavailable_after_load,
    reentrant_request,
};

struct CodeResidencyResult {
    CodeResidencyStatus status;
    recomp_func_t *function;
};

struct CodeResidencyDependencies {
    using FindResident = recomp_func_t *(*)(std::int32_t vram);
    using FindDeclared = recomp_func_t *(*)(std::int32_t vram);
    using FindNativeResident = recomp_func_t *(*)(std::int32_t vram);
    using ValidateDeclaredEntry = bool (*)(
            std::uint8_t *rdram, std::int32_t vram,
            recomp_func_t *declared_function);
    using RequestAvailability = bool (*)(
            std::uint8_t *rdram, recomp_context *ctx, std::uint32_t vram);

    FindResident find_resident = nullptr;
    FindDeclared find_declared = nullptr;
    // Native entrypoints are explicitly registered by Rage Wars and stay
    // callable even when the generic overlay map evicts an overlapping unit.
    FindNativeResident find_native_resident = nullptr;
    ValidateDeclaredEntry validate_declared_entry = nullptr;
    RequestAvailability request_availability = nullptr;
};

// Owns the game-visible contract for generated callable availability. The
// current N64ModernRuntime overlay/TLB machinery is an adapter behind these
// dependencies, not the owner of the policy.
class RageWarsCodeResidency {
public:
    explicit RageWarsCodeResidency(CodeResidencyDependencies dependencies);

    [[nodiscard]] CodeResidencyResult resolve(
            std::uint8_t *rdram, recomp_context *ctx, std::int32_t vram) const;

private:
    CodeResidencyDependencies dependencies_;
};

[[nodiscard]] const char *code_residency_status_name(CodeResidencyStatus status);
// Installs the production adapter used by generated LOOKUP_FUNC calls.
void install_code_residency_service();
// Registers a Rage Wars-owned native entrypoint. This is distinct from a
// generated declaration and remains valid across transient overlay eviction.
void register_native_resident_callable(std::int32_t vram, recomp_func_t *function);

} // namespace rage_wars
