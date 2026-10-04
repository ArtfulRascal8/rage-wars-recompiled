#include "recomp.h"
#include "rage_wars_audio_telemetry.hpp"
extern "C" void xr64_audio_command_build_body(std::uint8_t* rdram, recomp_context* ctx);
extern "C" void trace_func_002B3974_r000B4574(std::uint8_t* rdram, recomp_context* ctx) {
    namespace t = xr64::rage_wars::audio::telemetry;
    t::context(t::guest_context(rdram));
    t::Scope timing(t::Timer::producer);
    xr64_audio_command_build_body(rdram, ctx);
}
