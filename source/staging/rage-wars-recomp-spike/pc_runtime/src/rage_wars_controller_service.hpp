#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rage_wars {

struct ControllerPortStatus {
    bool connected = false;
    bool controller_pak = false;
    std::uint16_t type = 0x0005;
};

// Rage Wars only needs a small save/profile startup contract. These values are
// intentionally native-facing; Joybus bytes remain at the compatibility edge.
enum class RageWarsSaveStartupState : std::uint8_t {
    NoController,
    ControllerNoPak,
    PakInsertedInitial,
    StartupHandshakeAccepted,
    Ready,
};

struct ControllerBackend {
    bool (*get_status)(int channel, ControllerPortStatus* status) = nullptr;
    bool (*get_input)(
            int channel, std::uint16_t* buttons, float* x, float* y) = nullptr;
    bool (*read_pak)(
            int channel, std::size_t offset, std::uint8_t* data, std::size_t size) = nullptr;
    bool (*write_pak)(
            int channel, std::size_t offset, const std::uint8_t* data, std::size_t size) = nullptr;
    void (*signal_si_completion)() = nullptr;
};

class RageWarsControllerService {
public:
    static constexpr std::uint32_t read_from_pif = 0;
    static constexpr std::uint32_t write_to_pif = 1;
    static constexpr std::size_t pif_size = 64;

    void configure(const ControllerBackend& backend);
    void reset();
    void observe_controller_port(bool connected, bool controller_pak);

    [[nodiscard]] RageWarsSaveStartupState save_startup_state() const;
    [[nodiscard]] bool save_profile_ready() const;

    [[nodiscard]] int submit_dma(
            std::uint8_t* rdram, std::size_t rdram_size,
            std::uint32_t direction, std::uint32_t guest_buffer);

    [[nodiscard]] static std::uint8_t pak_data_crc(
            const std::uint8_t* data, std::size_t size);

private:
    void process_commands(bool input_only = false);
    void process_command(
            int channel, std::uint8_t command,
            const std::uint8_t* request, std::size_t request_size,
            std::uint8_t* response, std::size_t response_size,
            std::uint8_t& receive_length);

    ControllerBackend backend_{};
    std::array<std::uint8_t, pif_size> pif_{};
    RageWarsSaveStartupState save_startup_state_ = RageWarsSaveStartupState::NoController;
    bool startup_protocol_write_seen_ = false;
    bool startup_protocol_read_seen_ = false;
};

RageWarsControllerService& controller_service();

} // namespace rage_wars
