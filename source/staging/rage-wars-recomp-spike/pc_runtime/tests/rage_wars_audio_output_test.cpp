#include "rage_wars_audio_output.hpp"
#include "rage_wars_port_options.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
using namespace xr64::rage_wars::audio;
struct FakeBackend final : Backend {
    unsigned init_calls=0,open_calls=0,close_calls=0,play_calls=0;
    unsigned init_failures=0,open_failures=0;
    std::uint32_t id=0,hz=0;
    std::size_t quantum=512,queued=0;
    bool paused=true,queue_failure=false,device_stopped=false;
    const std::int16_t* native_storage=nullptr;
    std::vector<std::int16_t> copy;
    std::mutex block_mutex;
    std::condition_variable block_cv;
    bool block_close=false,closing=false,release_close=false;
    bool block_open=false,opening=false,release_open=false;
    bool initialize() override { ++init_calls; if(init_failures){--init_failures;return false;}return true; }
    Device open(std::uint32_t rate) override {
        { std::unique_lock gate(block_mutex); opening=true; block_cv.notify_all();
          if(block_open)block_cv.wait(gate,[this]{return release_open;}); }
        ++open_calls;if(open_failures){--open_failures;return {};}
        ++id;hz=rate;queued=0;paused=true;device_stopped=false;
        return {id,rate,quantum};
    }
    void close(std::uint32_t) override {
        std::unique_lock lock(block_mutex);closing=true;block_cv.notify_all();
        if(block_close)block_cv.wait(lock,[this]{return release_close;});
        ++close_calls;
    }
    void pause(std::uint32_t device,bool value) override {assert(device==id);paused=value;if(!value)++play_calls;}
    int enqueue(std::uint32_t device,const std::int16_t* samples,std::size_t count) override {
        assert(device==id);
        if(queue_failure)return -1;
        if(native_storage)assert(native_storage==samples);else native_storage=samples;
        copy.assign(samples,samples+count);queued+=count/2;return 0;
    }
    std::size_t queued_frames(std::uint32_t device) override {assert(device==id);return queued;}
    bool stopped(std::uint32_t device) override {assert(device==id);return device_stopped;}
    const char* error() override {return "injected backend failure";}
};
void conversion_contract() {
    std::vector<std::int16_t> source(131072),pcm(source.size());
    for(unsigned i=0;i<65536;++i){source[2*i]=static_cast<std::int16_t>(static_cast<int>(i)-32768);source[2*i+1]=static_cast<std::int16_t>(32767-static_cast<int>(i));}
    const auto untouched=source;
    GainRamp ramp;
    ramp.convert(source.data(),source.size(),pcm.data(),1,22047);
    for(std::size_t i=0;i<source.size();++i) {
        const auto old=static_cast<std::int16_t>(std::clamp(std::round(static_cast<float>(source[i^1])*1.0F),-32768.0F,32767.0F));
        assert(pcm[i]==old);
    }
    for(float gain:{0.0F,0.1F,0.5F,0.9F,1.0F}) {
        ramp.reset(gain);ramp.convert(source.data(),source.size(),pcm.data(),gain,22047);
        for(std::size_t i=0;i<source.size();++i)
            assert(pcm[i]==static_cast<std::int16_t>(std::clamp(std::round(static_cast<float>(source[i^1])*gain),-32768.0F,32767.0F)));
    }
    assert(source==untouched);
    std::array<std::int16_t,800> constant,change;
    for(std::size_t i=0;i<constant.size();i+=2){constant[i]=16000;constant[i+1]=32000;}
    ramp.reset(1);ramp.convert(constant.data(),constant.size(),change.data(),0,22047);
    for(std::size_t frame=1;frame<220;++frame) {
        assert(change[2*frame]<=change[2*frame-2]);
        assert(change[2*frame-2]-change[2*frame]<=146);
        assert(std::abs(change[2*frame]-2*change[2*frame+1])<=1);
    }
    assert(change[438]==0&&ramp.current()==0);
    ramp.convert(constant.data(),200,change.data(),1,22047);
    const int before=change[198];
    ramp.convert(constant.data(),constant.size(),change.data(),0.25F,22047);
    assert(std::abs(change[0]-before)<100);
    assert(ramp.current()==0.25F);
    ramp.convert(constant.data(),constant.size(),change.data(),1,22047);
    assert(ramp.settled_unity());
    ramp.convert(constant.data(),constant.size(),change.data(),1,22047);
    assert(change[0]==32000&&change[1]==16000);
}
void priming_and_bound() {
    for(auto rate:{8000U,22047U,48000U,96000U})for(auto quantum:{512U,1024U,4096U}) {
        FakeBackend backend;backend.quantum=quantum;backend.copy.reserve(kMaxSubmissionFrames*2);
        Output output(backend);output.frequency(rate);output.service(0);
        assert(output.state()==OutputState::priming&&backend.paused);
        const auto target=std::max<std::size_t>((rate+29)/30,quantum*3);
        const auto frames=(rate+59)/60;
        std::vector<std::int16_t> source(frames*2,1234);const auto original=source;
        while(!output.fifo_full()) {
            assert(backend.paused&&backend.queued<target);
            assert(output.queue(source.data(),source.size(),1));
        }
        assert(backend.queued>=target&&backend.queued<target+frames);
        assert(backend.play_calls==0);output.service(1);
        assert(backend.play_calls==1&&output.state()==OutputState::playing&&!backend.paused);
        assert(!output.queue(source.data(),source.size(),1)); // No hidden queue growth at full.
        for(unsigned i=0;i<500;++i) {
            backend.queued-=std::min<std::size_t>(backend.queued,frames);
            if(!output.fifo_full())assert(output.queue(source.data(),source.size(),1));
            output.service(i+2);assert(backend.queued<target+frames);
        }
        assert(source==original&&backend.native_storage!=source.data());
        assert(!output.queue(source.data(),3,1));
        assert(!output.queue(source.data(),kMaxSubmissionFrames*2+2,1));
        assert(!output.queue(nullptr,2,1));output.shutdown();
        assert(output.fifo_full()&&!output.queue(source.data(),source.size(),1));
    }
}
void recovery_contract() {
    FakeBackend backend;backend.init_failures=2;
    Output output(backend);output.frequency(22047);
    output.service(0);assert(output.state()==OutputState::recovering&&output.fifo_full());
    output.service(249);assert(backend.init_calls==1);
    output.service(250);assert(backend.init_calls==2);
    output.service(749);assert(backend.init_calls==2);
    output.service(750);assert(output.state()==OutputState::priming&&backend.init_calls==3);
    std::array<std::int16_t,736> source{};source[0]=100;source[1]=200;
    assert(output.queue(source.data(),source.size(),1));
    backend.queue_failure=true;assert(!output.queue(source.data(),source.size(),1));
    assert(output.state()==OutputState::recovering&&output.fifo_full());
    output.service(800);assert(backend.close_calls==1);
    backend.queue_failure=false;backend.open_failures=1;
    output.service(1050);assert(output.state()==OutputState::recovering);
    output.service(1300);assert(output.state()==OutputState::priming);
    output.removed(999);assert(output.state()==OutputState::priming);
    output.removed(backend.id);assert(output.fifo_full());
    output.service(1400);output.service(1650);assert(output.state()==OutputState::priming);
    backend.device_stopped=true;output.service(1700);assert(output.fifo_full());
    output.service(1950);assert(output.state()==OutputState::priming);
    output.frequency(48000);output.service(2000);assert(backend.hz==48000&&output.rate()==48000);
    output.frequency(1);assert(output.rate()==48000);output.shutdown();
}
void close_does_not_block_producer() {
    FakeBackend backend;Output output(backend);output.service(0);
    backend.block_close=true;output.frequency(22047);
    auto owner=std::async(std::launch::async,[&]{output.service(1);});
    {std::unique_lock lock(backend.block_mutex);backend.block_cv.wait(lock,[&]{return backend.closing;});}
    std::array<std::int16_t,736> source{};
    auto producer=std::async(std::launch::async,[&]{assert(output.fifo_full());return output.queue(source.data(),source.size(),1);});
    assert(producer.wait_for(std::chrono::milliseconds(100))==std::future_status::ready);
    assert(!producer.get());
    {std::lock_guard lock(backend.block_mutex);backend.release_close=true;}backend.block_cv.notify_all();
    owner.get();output.shutdown();
}
void recovery_during_open() {
    FakeBackend backend; backend.block_open=true; Output output(backend);
    auto owner=std::async(std::launch::async,[&]{output.service(0);});
    {std::unique_lock lock(backend.block_mutex);backend.block_cv.wait(lock,[&]{return backend.opening;});}
    output.recover(); assert(output.fifo_full());
    {std::lock_guard lock(backend.block_mutex);backend.release_open=true;}backend.block_cv.notify_all();
    owner.get(); assert(output.state()==OutputState::recovering && backend.close_calls==1);
    output.service(1); assert(output.state()==OutputState::priming && backend.open_calls==2);
    output.shutdown();
}
void settings_publication() {
    using namespace xr64::rage_wars::port_options;
    initialize();set_master_volume(0.25F);assert(master_gain()==0.25F);
    std::atomic<bool> done{false};std::atomic<std::uint64_t> reads{0};
    std::thread reader([&]{while(!done.load()){const auto value=master_gain();assert(value>=0&&value<=1);++reads;}});
    for(unsigned i=0;i<40;++i)set_master_volume(i&1?0.9F:0.1F);
    done=true;reader.join();assert(reads>1000&&master_gain()==0.9F);
    set_master_volume(std::numeric_limits<float>::quiet_NaN());assert(master_gain()==1);
    set_master_volume(-4);assert(master_gain()==0);reset_to_defaults();assert(master_gain()==1);
}
int main() {
    conversion_contract();priming_and_bound();recovery_contract();close_does_not_block_producer();recovery_during_open();settings_publication();
    std::puts("PASS: full-range PCM/unity/stereo/immutability; 10ms ramp/retarget; 12 reachable priming/bounded queue cases; init/open/queue/removal/status/rate recovery; nonblocking close; published settings.");
}
