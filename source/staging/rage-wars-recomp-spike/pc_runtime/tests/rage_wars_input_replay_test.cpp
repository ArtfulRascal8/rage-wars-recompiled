#include "rage_wars_input_replay.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::vector<rage_wars::InputReplayTraceEvent> traces;

void record(const rage_wars::InputReplayTraceEvent& event) {
    traces.push_back(event);
}

std::filesystem::path write_replay(
        const std::string& name, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path);
    assert(output);
    output << content;
    return path;
}
}

int main() {
    rage_wars::RageWarsInputReplay replay;
    replay.set_trace_callback(record);

    std::string error;
    assert(replay.load_file("", error));
    assert(!replay.enabled());
    assert(replay.sample(0, 1) == 0);

    const auto a_path = write_replay(
            "xr64_rw076_a.replay",
            "# RW076 v1\n0 A press\n150 A release\n");
    assert(replay.load_file(a_path.string(), error));
    assert(replay.enabled());
    assert(replay.event_count() == 2);
    replay.arm(1000, 10);
    assert(replay.sample(1000, 10) == 0x8000U);
    assert(replay.sample(1149, 11) == 0x8000U);
    assert(replay.sample(1150, 12) == 0);
    assert(traces.size() == 2);
    assert(traces[0].button == "A" && traces[0].pressed);
    assert(traces[0].vi == 10 && traces[0].elapsed_ms == 0);
    assert(traces[1].button == "A" && !traces[1].pressed);
    assert(traces[1].vi == 12 && traces[1].elapsed_ms == 150);

    traces.clear();
    const auto start_path = write_replay(
            "xr64_rw076_start.replay",
            "0 START press\n200 START release\n");
    assert(replay.load_file(start_path.string(), error));
    replay.arm(5000, 20);
    assert(replay.sample(5000, 20) == 0x1000U);
    assert(replay.sample(5200, 21) == 0);
    assert(traces.size() == 2);
    assert(traces[0].button == "START" && traces[0].pressed);
    assert(traces[1].button == "START" && !traces[1].pressed);

    const auto menu_path = write_replay("xr64_rw102_menu.replay",
            "0 DOWN press\n200 DOWN release\n300 B press\n400 B release\n");
    assert(replay.load_file(menu_path.string(), error));
    replay.arm(0, 0);
    assert(replay.sample(0, 0) == 0x0400U);
    assert(replay.sample(200, 12) == 0);
    assert(replay.sample(300, 18) == 0x4000U);
    assert(replay.sample(400, 24) == 0);
    const auto analog_path=write_replay("xr64_motion_analog.replay",
            "0 STICK_X 0.25\n0 STICK_Y -0.8\n50 A press\n100 STICK_X 0\n100 STICK_Y 0\n150 A release\n");
    assert(replay.load_file(analog_path.string(),error));
    float x=9,y=9;
    assert(replay.sample(0,0,&x,&y)==0&&x==0&&y==0);
    replay.arm(1000,60);
    assert(replay.sample(1050,63,&x,&y)==0x8000&&x==0.25F&&y==-0.8F);
    assert(replay.sample(1100,66,&x,&y)==0x8000&&x==0&&y==0);
    assert(replay.sample(1150,69,&x,&y)==0);
    replay.disable();
    assert(replay.sample(1200,72,&x,&y)==0&&x==0&&y==0);
    const auto invalid_axis=write_replay("xr64_motion_invalid_axis.replay","0 STICK_X 1.5\n");
    assert(!replay.load_file(invalid_axis.string(),error));
    std::filesystem::remove(analog_path);std::filesystem::remove(invalid_axis);
    std::filesystem::remove(menu_path);
    std::filesystem::remove(a_path);
    std::filesystem::remove(start_path);
    std::cout << "rw076 input replay contract passed\n";
}
