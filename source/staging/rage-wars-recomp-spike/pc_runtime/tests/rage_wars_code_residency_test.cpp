#include "rage_wars_code_residency.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

constexpr std::int32_t kCallable = 0x0044AFE0;
constexpr std::int32_t kKernelCallable = static_cast<std::int32_t>(0x8044AFE0U);
recomp_func_t *resident_function = nullptr;
bool declared = false;
bool native_resident_available = false;
bool entry_available = false;
bool request_result = false;
int request_count = 0;
const rage_wars::RageWarsCodeResidency *reentrant_service = nullptr;
rage_wars::CodeResidencyStatus nested_status{};

void generated_callable(std::uint8_t *, recomp_context *) {
}

recomp_func_t *find_resident(std::int32_t vram) {
    return vram == kCallable ? resident_function : nullptr;
}

recomp_func_t *find_declared(std::int32_t vram) {
    return declared && vram == kCallable ? generated_callable : nullptr;
}

recomp_func_t *find_native_resident(std::int32_t vram) {
    return native_resident_available && vram == kCallable ? generated_callable : nullptr;
}

bool validate_declared_entry(
        std::uint8_t *, std::int32_t vram,
        recomp_func_t *declared_function) {
    return entry_available && vram == kCallable &&
            declared_function == generated_callable;
}

bool request(std::uint8_t *rdram, recomp_context *ctx, std::uint32_t vram) {
    ++request_count;
    if (reentrant_service != nullptr) {
        nested_status = reentrant_service
                ->resolve(rdram, ctx, static_cast<std::int32_t>(vram)).status;
    }
    if (request_result) {
        resident_function = generated_callable;
    }
    return request_result;
}

void reset() {
    resident_function = nullptr;
    declared = false;
    native_resident_available = false;
    entry_available = false;
    request_result = false;
    request_count = 0;
    reentrant_service = nullptr;
    nested_status = rage_wars::CodeResidencyStatus::resident;
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "RageWarsCodeResidency contract failure: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    std::uint8_t rdram[1]{};
    recomp_context context{};
    const rage_wars::RageWarsCodeResidency service({
        .find_resident = find_resident,
        .find_declared = find_declared,
        .find_native_resident = find_native_resident,
        .validate_declared_entry = validate_declared_entry,
        .request_availability = request,
    });

    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::resident)} == "resident",
            "resident status name changed");
    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::made_resident)} == "made-resident",
            "made-resident status name changed");
    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::native_resident)} == "native-resident",
            "native-resident status name changed");
    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::native_unit_available)} ==
                    "native-unit-available",
            "native-unit-available status name changed");
    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::unknown_unit)} == "unknown-unit",
            "unknown-unit status name changed");
    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::load_declined)} == "load-declined",
            "load-declined status name changed");
    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::unavailable_after_load)} ==
                    "unavailable-after-load",
            "unavailable-after-load status name changed");
    require(std::string_view{rage_wars::code_residency_status_name(
                    rage_wars::CodeResidencyStatus::reentrant_request)} ==
                    "reentrant-request",
            "reentrant-request status name changed");

    reset();
    resident_function = generated_callable;
    auto result = service.resolve(rdram, &context, kCallable);
    require(result.status == rage_wars::CodeResidencyStatus::resident,
            "resident callable was not returned directly");
    require(result.function == generated_callable && request_count == 0,
            "resident callable incorrectly requested a load");

    reset();
    declared = true;
    request_result = true;
    result = service.resolve(rdram, &context, kCallable);
    require(result.status == rage_wars::CodeResidencyStatus::made_resident,
            "declared callable was not made resident");
    require(result.function == generated_callable && request_count == 1,
            "availability request did not resolve exactly once");

    reset();
    declared = true;
    native_resident_available = true;
    result = service.resolve(rdram, &context, kCallable);
    require(result.status == rage_wars::CodeResidencyStatus::native_resident,
            "registered native resident was not retained across overlay eviction");
    require(result.function == generated_callable && request_count == 0,
            "registered native resident incorrectly requested transport");

    reset();
    declared = true;
    entry_available = true;
    result = service.resolve(rdram, &context, kCallable);
    require(result.status ==
                    rage_wars::CodeResidencyStatus::native_unit_available,
            "proven declared entry was not returned");
    require(result.function == generated_callable && request_count == 0,
            "proven declared entry incorrectly requested transport");

    reset();
    request_result = true;
    result = service.resolve(rdram, &context, kCallable);
    require(result.status == rage_wars::CodeResidencyStatus::made_resident &&
                    result.function == generated_callable && request_count == 1,
            "unknown KUSEG callable did not use the guest availability contract");

    reset();
    declared = true;
    result = service.resolve(rdram, &context, kCallable);
    require(result.status == rage_wars::CodeResidencyStatus::load_declined &&
                    request_count == 1,
            "declined availability request reported the wrong outcome");

    reset();
    declared = true;
    const auto no_publish_request = +[](
            std::uint8_t *, recomp_context *, std::uint32_t) {
        ++request_count;
        return true;
    };
    const rage_wars::RageWarsCodeResidency incomplete_service({
        .find_resident = find_resident,
        .find_declared = find_declared,
        .validate_declared_entry = validate_declared_entry,
        .request_availability = no_publish_request,
    });
    result = incomplete_service.resolve(rdram, &context, kCallable);
    require(result.status ==
                    rage_wars::CodeResidencyStatus::unavailable_after_load,
            "successful request without publication was accepted");


    reset();
    result = service.resolve(rdram, &context, kKernelCallable);
    require(result.status == rage_wars::CodeResidencyStatus::unknown_unit &&
                    request_count == 0,
            "unknown KSEG callable reached the guest availability producer");

    reset();
    declared = true;
    reentrant_service = &service;
    result = service.resolve(rdram, &context, kCallable);
    require(nested_status ==
                    rage_wars::CodeResidencyStatus::reentrant_request,
            "recursive availability request was not rejected");
    require(result.status == rage_wars::CodeResidencyStatus::load_declined,
            "outer declined request reported the wrong outcome");

    std::cout << "RageWarsCodeResidency contract: PASS\n";
    return EXIT_SUCCESS;
}
