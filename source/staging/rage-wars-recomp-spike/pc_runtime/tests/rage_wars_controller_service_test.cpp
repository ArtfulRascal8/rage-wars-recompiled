#include <rage_wars_controller_service.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {

std::array<std::uint8_t, 32 * 1024> pak{};
int pak_reads = 0;
int pak_writes = 0;
int completions = 0;
std::uint16_t input_buttons = 0;
float input_x = 0.0F;
float input_y = 0.0F;

bool status(int channel, rage_wars::ControllerPortStatus* out) {
    if (channel != 0 || out == nullptr) {
        return false;
    }
    out->connected = true;
    out->controller_pak = true;
    out->type = 0x0005;
    return true;
}

bool input(int channel, std::uint16_t* buttons, float* x, float* y) {
    if (channel != 0) {
        return false;
    }
    *buttons = input_buttons;
    *x = input_x;
    *y = input_y;
    return true;
}

bool read_pak(
        int channel, std::size_t offset, std::uint8_t* data, std::size_t size) {
    if (channel != 0 || offset + size > pak.size()) {
        return false;
    }
    std::copy_n(pak.data() + offset, size, data);
    ++pak_reads;
    return true;
}

bool write_pak(
        int channel, std::size_t offset,
        const std::uint8_t* data, std::size_t size) {
    if (channel != 0 || offset + size > pak.size()) {
        return false;
    }
    std::copy_n(data, size, pak.data() + offset);
    ++pak_writes;
    return true;
}

void complete() {
    ++completions;
}

void guest_write(
        std::vector<std::uint8_t>& rdram, std::uint32_t base,
        std::size_t offset, std::uint8_t value) {
    rdram[(base + static_cast<std::uint32_t>(offset)) ^ 3U] = value;
}

std::uint8_t guest_read(
        const std::vector<std::uint8_t>& rdram, std::uint32_t base,
        std::size_t offset) {
    return rdram[(base + static_cast<std::uint32_t>(offset)) ^ 3U];
}

void transfer(
        rage_wars::RageWarsControllerService& service,
        std::vector<std::uint8_t>& rdram, std::uint32_t base) {
    const int before = completions;
    assert(service.submit_dma(
            rdram.data(), rdram.size(),
            rage_wars::RageWarsControllerService::write_to_pif, base) == 0);
    assert(completions == before + 1);
    assert(service.submit_dma(
            rdram.data(), rdram.size(),
            rage_wars::RageWarsControllerService::read_from_pif, base) == 0);
    assert(completions == before + 2);
}

} // namespace

int main() {
    rage_wars::RageWarsControllerService service;
    service.configure({status, input, read_pak, write_pak, complete});
    std::vector<std::uint8_t> rdram(8 * 1024 * 1024);
    constexpr std::uint32_t buffer = 0x00100000;

    guest_write(rdram, buffer, 0, 1);
    guest_write(rdram, buffer, 1, 3);
    guest_write(rdram, buffer, 2, 0);
    guest_write(rdram, buffer, 6, 0xFE);
    transfer(service, rdram, buffer);
    assert(guest_read(rdram, buffer, 3) == 0x05);
    assert(guest_read(rdram, buffer, 4) == 0x00);
    assert(guest_read(rdram, buffer, 5) == 0x03);

    for (std::size_t index = 0; index < 64; ++index) {
        guest_write(rdram, buffer, index, 0);
    }
    guest_write(rdram, buffer, 0, 35);
    guest_write(rdram, buffer, 1, 1);
    guest_write(rdram, buffer, 2, 3);
    guest_write(rdram, buffer, 3, 0x80);
    guest_write(rdram, buffer, 4, 0x01);
    for (std::size_t index = 0; index < 32; ++index) {
        guest_write(rdram, buffer, 5 + index, 0xFE);
    }
    guest_write(rdram, buffer, 38, 0xFE);
    transfer(service, rdram, buffer);
    assert(guest_read(rdram, buffer, 37) == 0xE1);
    assert(pak_writes == 0);
    assert(service.save_startup_state() == rage_wars::RageWarsSaveStartupState::StartupHandshakeAccepted);

    for (std::size_t index = 0; index < 64; ++index) guest_write(rdram, buffer, index, 0);
    guest_write(rdram, buffer, 0, 3);
    guest_write(rdram, buffer, 1, 33);
    guest_write(rdram, buffer, 2, 2);
    guest_write(rdram, buffer, 3, 0x80);
    guest_write(rdram, buffer, 4, 0x01);
    guest_write(rdram, buffer, 38, 0xFE);
    transfer(service, rdram, buffer);
    for (std::size_t index = 0; index < 32; ++index) assert(guest_read(rdram, buffer, 5 + index) == 0);
    assert(guest_read(rdram, buffer, 37) == 0x00);
    assert(pak_reads == 0);
    assert(service.save_profile_ready());

    for (std::size_t index = 0; index < 64; ++index) guest_write(rdram, buffer, index, 0);
    guest_write(rdram, buffer, 0, 1);
    guest_write(rdram, buffer, 1, 3);
    guest_write(rdram, buffer, 2, 0);
    guest_write(rdram, buffer, 6, 0xFE);
    transfer(service, rdram, buffer);
    assert(guest_read(rdram, buffer, 5) == 0x01);

    for (std::size_t index = 0; index < 64; ++index) guest_write(rdram, buffer, index, 0);
    input_buttons = 0x9000U;
    input_x = 1.0F;
    input_y = -1.0F;
    guest_write(rdram, buffer, 0, 1);
    guest_write(rdram, buffer, 1, 4);
    guest_write(rdram, buffer, 2, 1);
    guest_write(rdram, buffer, 7, 0xFE);
    transfer(service, rdram, buffer);
    assert(guest_read(rdram, buffer, 3) == 0x90);
    assert(guest_read(rdram, buffer, 4) == 0x00);
    assert(guest_read(rdram, buffer, 5) == 0x50);
    assert(guest_read(rdram, buffer, 6) == 0xB0);

    input_buttons = 0;
    input_x = 0.0F;
    input_y = 0.0F;
    const int before_read = completions;
    assert(service.submit_dma(
            rdram.data(), rdram.size(),
            rage_wars::RageWarsControllerService::read_from_pif, buffer) == 0);
    assert(completions == before_read + 1);
    assert(guest_read(rdram, buffer, 3) == 0);
    assert(guest_read(rdram, buffer, 4) == 0);
    assert(guest_read(rdram, buffer, 5) == 0);
    assert(guest_read(rdram, buffer, 6) == 0);

    pak[0x20] = 0xA5;
    for (std::size_t index = 0; index < 64; ++index) guest_write(rdram, buffer, index, 0);
    guest_write(rdram, buffer, 0, 3);
    guest_write(rdram, buffer, 1, 33);
    guest_write(rdram, buffer, 2, 2);
    guest_write(rdram, buffer, 3, 0x00);
    guest_write(rdram, buffer, 4, 0x20);
    guest_write(rdram, buffer, 38, 0xFE);
    transfer(service, rdram, buffer);
    assert(guest_read(rdram, buffer, 5) == 0xA5);
    assert(pak_reads == 1);

    std::array<std::uint8_t, 32> values{};
    values.fill(0x80);
    assert(rage_wars::RageWarsControllerService::pak_data_crc(
            values.data(), values.size()) == 0xB8);
    // Six write/read pairs and one read-only refresh: 6 * 2 + 1.
    // Per-DMA checks above also catch a missing or duplicate notification.
    assert(completions == 13);
    return 0;
}
