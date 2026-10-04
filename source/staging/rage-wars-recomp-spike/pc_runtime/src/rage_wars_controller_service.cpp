#include <rage_wars_controller_service.hpp>
#include <ultramodern/ultramodern.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace rage_wars {
namespace {

constexpr std::size_t controller_pak_size = 32 * 1024;
constexpr std::size_t startup_protocol_address = 0x8000;
RageWarsControllerService g_controller_service;

std::uint8_t& guest_byte(
        std::uint8_t* rdram, std::uint32_t physical, std::size_t offset) {
    return rdram[(physical + static_cast<std::uint32_t>(offset)) ^ 3U];
}

std::int8_t stick_axis(float value) {
    const float bounded = std::clamp(value, -1.0F, 1.0F);
    return static_cast<std::int8_t>(std::lround(bounded * 80.0F));
}

const char* save_startup_state_name(RageWarsSaveStartupState state) {
    switch (state) {
    case RageWarsSaveStartupState::NoController: return "NoController";
    case RageWarsSaveStartupState::ControllerNoPak: return "ControllerNoPak";
    case RageWarsSaveStartupState::PakInsertedInitial: return "PakInsertedInitial";
    case RageWarsSaveStartupState::StartupHandshakeAccepted: return "StartupHandshakeAccepted";
    case RageWarsSaveStartupState::Ready: return "Ready";
    }
    return "Unknown";
}

} // namespace

void RageWarsControllerService::configure(const ControllerBackend& backend) {
    backend_ = backend;
    reset();
}

void RageWarsControllerService::reset() {
    pif_.fill(0);
    save_startup_state_ = RageWarsSaveStartupState::NoController;
    startup_protocol_write_seen_ = false;
    startup_protocol_read_seen_ = false;
}

void RageWarsControllerService::observe_controller_port(bool connected, bool controller_pak) {
    if (!connected) {
        save_startup_state_ = RageWarsSaveStartupState::NoController;
        startup_protocol_write_seen_ = false;
        startup_protocol_read_seen_ = false;
        return;
    }
    if (!controller_pak) {
        save_startup_state_ = RageWarsSaveStartupState::ControllerNoPak;
        startup_protocol_write_seen_ = false;
        startup_protocol_read_seen_ = false;
        return;
    }
    if (save_startup_state_ == RageWarsSaveStartupState::NoController ||
            save_startup_state_ == RageWarsSaveStartupState::ControllerNoPak) {
        save_startup_state_ = RageWarsSaveStartupState::PakInsertedInitial;
    }
}

RageWarsSaveStartupState RageWarsControllerService::save_startup_state() const {
    return save_startup_state_;
}

bool RageWarsControllerService::save_profile_ready() const {
    return save_startup_state_ == RageWarsSaveStartupState::Ready;
}

std::uint8_t RageWarsControllerService::pak_data_crc(
        const std::uint8_t* data, std::size_t size) {
    std::uint8_t crc = 0;
    for (std::size_t byte = 0; byte <= size; ++byte) {
        for (std::uint8_t mask = 0x80; mask != 0; mask >>= 1U) {
            const bool feedback = (crc & 0x80U) != 0;
            crc = static_cast<std::uint8_t>(crc << 1U);
            if (byte < size && (data[byte] & mask) != 0) {
                crc |= 1U;
            }
            if (feedback) {
                crc ^= 0x85U;
            }
        }
    }
    return crc;
}

int RageWarsControllerService::submit_dma(
        std::uint8_t* rdram, std::size_t rdram_size,
        std::uint32_t direction, std::uint32_t guest_buffer) {
    if (rdram == nullptr || rdram_size < pif_size) {
        return -1;
    }
    const std::uint32_t physical = guest_buffer & 0x1FFFFFFFU;
    if (physical > rdram_size - pif_size) {
        return -1;
    }

    {
        std::ostringstream detail;
        detail << "stage=si-dma phase=begin direction=" << direction
               << " guest_buffer=0x" << std::uppercase << std::hex << guest_buffer;
        ultramodern::runtime_trace(detail.str());
    }

    if (direction == write_to_pif) {
        for (std::size_t index = 0; index < pif_size; ++index) {
            pif_[index] = guest_byte(rdram, physical, index);
        }
        process_commands();
    } else if (direction == read_from_pif) {
        // Rage Wars leaves its read-buttons command resident in PIF RAM and
        // repeatedly reads the refreshed response. Do not repeat Pak commands.
        process_commands(true);
        for (std::size_t index = 0; index < pif_size; ++index) {
            guest_byte(rdram, physical, index) = pif_[index];
        }
        std::ostringstream detail;
        detail << "stage=si-memory-write guest_buffer=0x" << std::uppercase << std::hex
               << guest_buffer << " bytes=" << std::dec << pif_size;
        ultramodern::runtime_trace(detail.str());
    } else {
        return -1;
    }

    if (backend_.signal_si_completion != nullptr) {
        ultramodern::runtime_trace("stage=si-completion phase=generated");
        backend_.signal_si_completion();
    }
    return 0;
}

void RageWarsControllerService::process_commands(bool input_only) {
    std::size_t cursor = 0;
    int channel = 0;
    while (cursor < pif_.size()) {
        const std::uint8_t transmit_field = pif_[cursor];
        if (transmit_field == 0xFEU) {
            break;
        }
        if (transmit_field == 0xFFU) {
            ++cursor;
            continue;
        }
        if (transmit_field == 0x00U || transmit_field == 0xFDU) {
            ++cursor;
            ++channel;
            continue;
        }
        if (cursor + 2 > pif_.size()) {
            break;
        }

        const std::size_t transmit = transmit_field & 0x3FU;
        std::uint8_t& receive_field = pif_[cursor + 1];
        const std::size_t receive = receive_field & 0x3FU;
        const std::size_t request_offset = cursor + 2;
        const std::size_t response_offset = request_offset + transmit;
        const std::size_t next = response_offset + receive;
        if (transmit == 0 || next > pif_.size()) {
            break;
        }

        const std::uint8_t command = pif_[request_offset];
        if (!input_only || command == 0x01U) {
            process_command(
                    channel, command,
                    pif_.data() + request_offset, transmit,
                    pif_.data() + response_offset, receive,
                    receive_field);
        }
        cursor = next;
        ++channel;
    }
}

void RageWarsControllerService::process_command(
        int channel, std::uint8_t command,
        const std::uint8_t* request, std::size_t request_size,
        std::uint8_t* response, std::size_t response_size,
        std::uint8_t& receive_length) {
    ControllerPortStatus status{};
    const bool connected = backend_.get_status != nullptr &&
            backend_.get_status(channel, &status) && status.connected;
    if (channel == 0) {
        observe_controller_port(connected, connected && status.controller_pak);
    }
    if (!connected) {
        receive_length = static_cast<std::uint8_t>(
                (receive_length & 0x3FU) | 0x80U);
        return;
    }
    receive_length &= 0x3FU;

    if ((command == 0x00U || command == 0xFFU) && response_size >= 3) {
        response[0] = static_cast<std::uint8_t>(status.type & 0xFFU);
        response[1] = static_cast<std::uint8_t>(status.type >> 8U);
        response[2] = status.controller_pak
                ? static_cast<std::uint8_t>(save_profile_ready() ? 1U : 3U)
                : 2U;
        {
            std::ostringstream detail;
            detail << "stage=pif-response operation=status channel=" << channel
                   << " response=" << std::uppercase << std::hex
                   << std::setw(2) << std::setfill('0') << static_cast<unsigned>(response[0])
                   << " " << std::setw(2) << static_cast<unsigned>(response[1])
                   << " " << std::setw(2) << static_cast<unsigned>(response[2])
                   << " startup_state=" << save_startup_state_name(save_startup_state_)
                   << " save_profile_ready=" << std::dec << (save_profile_ready() ? 1 : 0);
            ultramodern::runtime_trace(detail.str());
        }
        return;
    }

    if (command == 0x01U && response_size >= 4) {
        std::uint16_t buttons = 0;
        float x = 0.0F;
        float y = 0.0F;
        if (backend_.get_input != nullptr) {
            backend_.get_input(channel, &buttons, &x, &y);
        }
        response[0] = static_cast<std::uint8_t>(buttons >> 8U);
        response[1] = static_cast<std::uint8_t>(buttons & 0xFFU);
        response[2] = static_cast<std::uint8_t>(stick_axis(x));
        response[3] = static_cast<std::uint8_t>(stick_axis(y));
        return;
    }

    if (command == 0x02U && request_size >= 3 && response_size >= 33) {
        const std::uint16_t encoded = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(request[1]) << 8U | request[2]);
        const std::size_t address = encoded & 0xFFE0U;
        const auto state_before = save_startup_state_;
        std::fill(response, response + 32, std::uint8_t{0});
        bool ok = false;
        bool backend_attempted = false;
        const bool startup_protocol = status.controller_pak && address == startup_protocol_address;
        if (startup_protocol) {
            startup_protocol_read_seen_ = true;
            save_startup_state_ = startup_protocol_write_seen_
                    ? RageWarsSaveStartupState::Ready
                    : RageWarsSaveStartupState::StartupHandshakeAccepted;
            ok = true;
        } else if (status.controller_pak && address + 32 <= controller_pak_size &&
                backend_.read_pak != nullptr) {
            backend_attempted = true;
            ok = backend_.read_pak(channel, address, response, 32);
        }
        response[32] = pak_data_crc(response, 32);
        {
            std::ostringstream detail;
            detail << "stage=rw074-save-startup operation=pak-read channel=" << channel
                   << " raw_address=0x" << std::uppercase << std::hex << encoded
                   << " address=0x" << address
                   << " classification=" << (startup_protocol ? "startup-control" : "memory-backed")
                   << " backend_attempted=" << std::dec << (backend_attempted ? 1 : 0)
                   << " backend_offset=" << (backend_attempted ? std::to_string(address) : "none")
                   << " state_before=" << save_startup_state_name(state_before)
                   << " state_after=" << save_startup_state_name(save_startup_state_)
                   << " result=" << (ok ? 0 : 1)
                   << " response_crc=0x" << std::uppercase << std::hex
                   << std::setw(2) << std::setfill('0') << static_cast<unsigned>(response[32]);
            ultramodern::runtime_trace(detail.str());
        }
        return;
    }

    if (command == 0x03U && request_size >= 35 && response_size >= 1) {
        const std::uint16_t encoded = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(request[1]) << 8U | request[2]);
        const std::size_t address = encoded & 0xFFE0U;
        const std::uint8_t* data = request + 3;
        const auto state_before = save_startup_state_;
        bool ok = false;
        bool backend_attempted = false;
        const bool startup_protocol = status.controller_pak && address == startup_protocol_address;
        if (startup_protocol) {
            startup_protocol_write_seen_ = true;
            save_startup_state_ = startup_protocol_read_seen_
                    ? RageWarsSaveStartupState::Ready
                    : RageWarsSaveStartupState::StartupHandshakeAccepted;
            ok = true;
        } else if (status.controller_pak && address + 32 <= controller_pak_size &&
                backend_.write_pak != nullptr) {
            backend_attempted = true;
            ok = backend_.write_pak(channel, address, data, 32);
        }
        response[0] = pak_data_crc(data, 32);
        {
            std::ostringstream detail;
            detail << "stage=rw074-save-startup operation=pak-write channel=" << channel
                   << " raw_address=0x" << std::uppercase << std::hex << encoded
                   << " address=0x" << address
                   << " classification=" << (startup_protocol ? "startup-control" : "memory-backed")
                   << " backend_attempted=" << std::dec << (backend_attempted ? 1 : 0)
                   << " backend_offset=" << (backend_attempted ? std::to_string(address) : "none")
                   << " state_before=" << save_startup_state_name(state_before)
                   << " state_after=" << save_startup_state_name(save_startup_state_)
                   << " result=" << (ok ? 0 : 1)
                   << " response_crc=0x" << std::uppercase << std::hex
                   << std::setw(2) << std::setfill('0') << static_cast<unsigned>(response[0]);
            ultramodern::runtime_trace(detail.str());
        }
        return;
    }

    receive_length = static_cast<std::uint8_t>(
            (receive_length & 0x3FU) | 0x40U);
}

RageWarsControllerService& controller_service() {
    return g_controller_service;
}

} // namespace rage_wars
