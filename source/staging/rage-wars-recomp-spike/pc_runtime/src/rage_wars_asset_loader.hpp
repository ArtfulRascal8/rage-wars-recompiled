#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace rage_wars {

// Game-owned presentation of the asset transport contract.  The generated
// game code can observe whether a transfer is active; native code owns the
// copy, code/data publication, and completion ordering behind this surface.
class RageWarsAssetLoader {
public:
    static constexpr std::uint32_t kPiStatusAddress = 0xA4600010U;
    static constexpr std::uint32_t kCartRomBaseAddress = 0xB0000000U;
    static constexpr std::uint32_t kTransferBusy = 0x00000001U;

    void reset();
    void begin_transfer();
    void complete_transfer();
    void bind_cart_rom(std::span<const std::uint8_t> cart_rom);

    [[nodiscard]] bool transfer_active() const;
    [[nodiscard]] std::uint32_t status() const;

    // Returns a guest-visible endpoint only for an explicitly supported
    // asset-loader observation.  Other PI registers remain unsupported until
    // their game-visible contract is defined.
    [[nodiscard]] std::uint8_t *resolve_guest_address(std::uint32_t address) const;

private:
    void set_status(std::uint32_t status);

    alignas(std::uint32_t) std::array<std::uint32_t, 1> status_word_{};
    std::span<const std::uint8_t> cart_rom_{};
};

RageWarsAssetLoader &asset_loader();
void install_asset_loader_service();
std::uint8_t *resolve_asset_loader_guest_address(std::uint32_t address);

} // namespace rage_wars
