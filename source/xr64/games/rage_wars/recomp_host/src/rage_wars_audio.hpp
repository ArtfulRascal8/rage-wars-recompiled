#pragma once
#include <cstddef>
#include <cstdint>
namespace xr64::rage_wars::audio {
bool enabled();
void initialize(); // Host startup, after selecting the settings/data paths.
bool ready();
bool fifo_full();
void queue(std::int16_t* samples, std::size_t count);
std::size_t remaining();
void frequency(std::uint32_t hz);
std::uint32_t rate();
void device_removed(std::uint32_t id);
void request_recovery();
void begin_submission() noexcept;
bool submission_succeeded() noexcept;
void shutdown();
}
