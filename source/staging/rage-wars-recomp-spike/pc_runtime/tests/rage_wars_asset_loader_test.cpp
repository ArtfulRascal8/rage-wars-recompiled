#include "rage_wars_asset_loader.hpp"

#include <cstdlib>
#include <iostream>
#include <array>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "RageWarsAssetLoader contract failure: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    rage_wars::RageWarsAssetLoader service;

    service.reset();
    require(!service.transfer_active() && service.status() == 0,
            "a reset loader must be idle");
    require(service.resolve_guest_address(0xA4600000U) == nullptr,
            "an undefined PI register became guest-visible");

    std::uint8_t *status_endpoint = service.resolve_guest_address(
            rage_wars::RageWarsAssetLoader::kPiStatusAddress);
    require(status_endpoint != nullptr,
            "the asset-loader status observation is not available");
    require(*reinterpret_cast<std::uint32_t *>(status_endpoint) == 0,
            "the idle status observation is not zero");

    service.begin_transfer();
    require(service.transfer_active() && service.status() ==
                    rage_wars::RageWarsAssetLoader::kTransferBusy,
            "a started transfer is not observable as busy");
    require(*reinterpret_cast<std::uint32_t *>(status_endpoint) ==
                    rage_wars::RageWarsAssetLoader::kTransferBusy,
            "the guest endpoint did not observe transfer start");

    service.complete_transfer();
    require(!service.transfer_active() && service.status() == 0,
            "completion did not return the loader to idle");
    require(*reinterpret_cast<std::uint32_t *>(status_endpoint) == 0,
            "the guest endpoint observed completion before idle state");

    const std::array<std::uint8_t, 8> cart_rom{
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    };
    service.bind_cart_rom(cart_rom);
    std::uint8_t *word_transaction = service.resolve_guest_address(
            rage_wars::RageWarsAssetLoader::kCartRomBaseAddress);
    require(word_transaction != nullptr &&
                    *reinterpret_cast<std::uint32_t *>(word_transaction) == 0x12345678U,
            "cart read transaction did not preserve word order");
    std::uint8_t *byte_transaction = service.resolve_guest_address(
            rage_wars::RageWarsAssetLoader::kCartRomBaseAddress + (7U ^ 3U));
    require(byte_transaction != nullptr && *byte_transaction == 0xF0U,
            "cart read transaction did not preserve byte order");
    require(service.resolve_guest_address(
                    rage_wars::RageWarsAssetLoader::kCartRomBaseAddress + 8U) == nullptr,
            "cart read transaction exposed data outside the bound asset source");


    std::cout << "RageWarsAssetLoader contract: PASS\n";
    return EXIT_SUCCESS;
}
