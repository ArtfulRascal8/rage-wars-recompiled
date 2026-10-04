#include "rage_wars_asset_loader.hpp"

namespace rage_wars {
namespace {

RageWarsAssetLoader g_asset_loader;
thread_local std::array<std::uint8_t, 4> cart_read_transaction{};

} // namespace

void RageWarsAssetLoader::reset() {
    set_status(0);
}

void RageWarsAssetLoader::begin_transfer() {
    set_status(kTransferBusy);
}

void RageWarsAssetLoader::complete_transfer() {
    set_status(0);
}

void RageWarsAssetLoader::bind_cart_rom(std::span<const std::uint8_t> cart_rom) {
    cart_rom_ = cart_rom;
}

bool RageWarsAssetLoader::transfer_active() const {
    return status() == kTransferBusy;
}

std::uint32_t RageWarsAssetLoader::status() const {
    return status_word_.front();
}

std::uint8_t *RageWarsAssetLoader::resolve_guest_address(std::uint32_t address) const {
    if (address == kPiStatusAddress) {
        return reinterpret_cast<std::uint8_t *>(const_cast<std::uint32_t *>(status_word_.data()));
    }
    if (address < kCartRomBaseAddress) {
        return nullptr;
    }

    const std::uint32_t offset = address - kCartRomBaseAddress;
    if (offset >= cart_rom_.size()) {
        return nullptr;
    }

    const std::uint32_t transaction_base = offset & ~3U;
    if (transaction_base + 4U > cart_rom_.size()) {
        return nullptr;
    }
    for (std::uint32_t raw_byte = 0; raw_byte < cart_read_transaction.size(); ++raw_byte) {
        cart_read_transaction[raw_byte] = cart_rom_[transaction_base + (raw_byte ^ 3U)];
    }
    return cart_read_transaction.data() + (offset & 3U);
}

void RageWarsAssetLoader::set_status(std::uint32_t status) {
    status_word_.front() = status;
}

RageWarsAssetLoader &asset_loader() {
    return g_asset_loader;
}

void install_asset_loader_service() {
    asset_loader().reset();
}

std::uint8_t *resolve_asset_loader_guest_address(std::uint32_t address) {
    return asset_loader().resolve_guest_address(address);
}

} // namespace rage_wars
