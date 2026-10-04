#include "rage_wars_code_residency.hpp"

#include "gate5_runtime.hpp"
#include "librecomp/overlays.hpp"
#include <cstdio>
#include <unordered_map>

namespace rage_wars {
namespace {

std::unordered_map<std::int32_t, recomp_func_t *> native_resident_callables;

recomp_func_t *find_resident(std::int32_t vram) {
    return recomp::overlays::find_resident_callable(vram);
}

recomp_func_t *find_declared(std::int32_t vram) {
    return recomp::overlays::get_generated_callable(vram);
}

recomp_func_t *find_native_resident(std::int32_t vram) {
    const auto found = native_resident_callables.find(vram);
    return found != native_resident_callables.end() ? found->second : nullptr;
}

bool validate_declared_entry(
        std::uint8_t *rdram, std::int32_t vram,
        recomp_func_t *declared_function) {
    return rage_wars_validate_declared_entry_provenance(
            rdram, vram, declared_function);
}

bool request_availability(
        std::uint8_t *rdram, recomp_context *ctx, std::uint32_t vram) {
    // Temporary adapter: the existing guest fault bridge remains the producer
    // of availability until native overlay/data residency replaces it.
    return rage_wars_surface_unresolved_callable_fault(rdram, ctx, vram);
}

RageWarsCodeResidency code_residency({
    .find_resident = find_resident,
    .find_declared = find_declared,
    .find_native_resident = find_native_resident,
    .validate_declared_entry = validate_declared_entry,
    .request_availability = request_availability,
});

recomp_func_t *resolve_callable(
        std::uint8_t *rdram, recomp_context *ctx, std::int32_t vram) {
    const CodeResidencyResult result = code_residency.resolve(rdram, ctx, vram);
    if (result.function == nullptr) {
        // One contract outcome per unresolved request; this is not an instruction trace.
        std::fprintf(stderr,
                "RW042_CODE_RESIDENCY guest_pc=0x%08X outcome=%s\n",
                static_cast<unsigned>(vram), code_residency_status_name(result.status));
        std::fflush(stderr);
    }
    return result.function;
}

} // namespace

void register_native_resident_callable(std::int32_t vram, recomp_func_t *function) {
    native_resident_callables[vram] = function;
    recomp::overlays::add_loaded_function(vram, function);
}

void install_code_residency_service() {
    recomp::overlays::set_callable_resolver(resolve_callable);
}

} // namespace rage_wars
