#include "rage_wars_audio.hpp"
#include "rage_wars_port_options.hpp"
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace xr64::rage_wars;
int main() {
    _putenv_s("XR64_AUDIO","1");
    _putenv_s("SDL_AUDIODRIVER","xr64_deliberately_invalid_driver");
    audio::initialize();audio::frequency(22047);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    assert(!audio::ready()&&audio::fifo_full());
    _putenv_s("SDL_AUDIODRIVER","");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!audio::ready()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(audio::ready());
    std::jthread producer([](std::stop_token token) {
        std::array<std::int16_t,8192> samples{};double phase=0;
        while(!token.stop_requested()) {
            if(!audio::fifo_full()) {
                const auto hz=audio::rate();const auto frames=(hz+59)/60;
                for(std::size_t i=0;i<frames;++i){
                    samples[2*i]=static_cast<std::int16_t>(500*std::cos(phase));
                    samples[2*i+1]=static_cast<std::int16_t>(1000*std::sin(phase));
                    phase+=6.283185307179586*440/hz;
                }
                audio::begin_submission();audio::queue(samples.data(),frames*2);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    for(unsigned i=0;i<30;++i){port_options::set_master_volume(i&1?0.2F:0.8F);port_options::set_gamma(i&1?1.1F:1.2F);}
    audio::request_recovery();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));assert(audio::ready());
    audio::frequency(48000);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));assert(audio::ready()&&audio::rate()==48000);
    audio::frequency(22047);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));assert(audio::ready()&&audio::rate()==22047);
    producer.request_stop();producer.join();audio::shutdown();
    std::puts("PASS: real SDL2 initialization failure/retry, queued playback, persisted settings during submission, close/reopen recovery, two guest-rate changes, shutdown.");
}
