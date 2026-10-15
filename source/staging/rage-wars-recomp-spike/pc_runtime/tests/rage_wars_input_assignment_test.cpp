#include "rage_wars_input_assignment.hpp"

#include <cstdio>
#include <cstdlib>

using namespace xr64::rage_wars::input_assignment;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static Source src(const char* text) { Source s; if (!parse_source(text, s)) { std::fprintf(stderr, "unparsed %s\n", text); std::exit(2); } return s; }

int main() {
    // Parsing.
    CHECK(src("Auto").kind == Kind::Auto);
    CHECK(src(" KEYBOARD + mouse ").kind == Kind::KeyboardMouse);
    CHECK(src("kbm").kind == Kind::KeyboardMouse);
    CHECK((src("gamepad3") == Source{Kind::Gamepad, 3}));
    CHECK((src("pad") == Source{Kind::Gamepad, 1}));
    CHECK((src("Controller2") == Source{Kind::Gamepad, 2}));
    CHECK(src("vr").kind == Kind::Xr);
    CHECK(src("OFF").kind == Kind::Off);
    Source tmp;
    CHECK(!parse_source("gamepad5", tmp));
    CHECK(!parse_source("gamepad0", tmp));
    CHECK(!parse_source("joystick", tmp));
    CHECK(!parse_source("", tmp));

    // Config text: comments, both key spellings, bad lines ignored but counted.
    Assignment a;
    unsigned problems = 99;
    const bool clean = parse_config("# note\nplayer1 = gamepad2 ; trailing\nP2=keyboard\nplayer3 = vr\nplayer4 = bogus\nplayer9 = off\nnonsense\n", a, &problems);
    CHECK(!clean && problems == 3);
    CHECK((a.port[0] == Source{Kind::Gamepad, 2}));
    CHECK(a.port[1].kind == Kind::KeyboardMouse);
    CHECK(a.port[2].kind == Kind::Xr);
    CHECK(a.port[3].kind == Kind::Auto);  // bad value keeps the previous (default)
    Assignment round;
    CHECK(parse_config(serialize(a), round) && round == a);
    CHECK(parse_config("", round));

    // Defaults reproduce shipped behaviour: port 1 legacy, others extra gamepads in order.
    {
        Assignment d;
        auto e = resolve(d, {0, false});
        CHECK(e[0].route == Route::Legacy && e[0].connected);
        CHECK(e[1].route == Route::Off && e[2].route == Route::Off && e[3].route == Route::Off);
        e = resolve(d, {1, false});  // one pad: port 1 legacy already uses it
        CHECK(e[1].route == Route::Off);
        e = resolve(d, {3, false});  // three pads: pad1 -> legacy port 1, pads 2,3 -> ports 2,3
        CHECK(e[1].route == Route::Gamepad && e[1].pad == 2 && e[1].connected);
        CHECK(e[2].route == Route::Gamepad && e[2].pad == 3 && e[2].connected);
        CHECK(e[3].route == Route::Off);
        e = resolve(d, {4, true});
        CHECK(e[3].route == Route::Gamepad && e[3].pad == 4);
    }
    // Mixed: P1 keyboard, P2 gamepad1, P3 VR.
    {
        Assignment m;
        m.port[0] = src("keyboard"); m.port[1] = src("gamepad1"); m.port[2] = src("vr");
        auto e = resolve(m, {1, true});
        CHECK(e[0].route == Route::KeyboardMouse && e[0].connected);
        CHECK(e[1].route == Route::Gamepad && e[1].pad == 1 && e[1].connected);
        CHECK(e[2].route == Route::Xr && e[2].connected);
        CHECK(e[3].route == Route::Off);
        CHECK(keyboard_mouse_port(e) == 0);
        CHECK(xr_port(e) == 2);
    }
    // VR requested without a headset: plugged in but not connected.
    {
        Assignment m; m.port[1] = src("vr");
        auto e = resolve(m, {0, false});
        CHECK(e[1].route == Route::Xr && !e[1].connected);
    }
    // Explicit port 1 gamepad frees keyboard for port 2; autos skip claimed pads.
    {
        Assignment m; m.port[0] = src("gamepad2"); m.port[1] = src("keyboard");
        auto e = resolve(m, {3, false});
        CHECK(e[0].route == Route::Gamepad && e[0].pad == 2);
        CHECK(e[1].route == Route::KeyboardMouse);
        CHECK(e[2].route == Route::Gamepad && e[2].pad == 1);  // first unclaimed pad
        CHECK(e[3].route == Route::Gamepad && e[3].pad == 3);
        CHECK(keyboard_mouse_port(e) == 1);
    }
    // Duplicate claims: lower port wins, the other is unplugged and flagged.
    {
        Assignment m; m.port[0] = src("keyboard"); m.port[2] = src("keyboard"); m.port[1] = src("gamepad1"); m.port[3] = src("gamepad1");
        auto e = resolve(m, {2, false});
        CHECK(e[0].route == Route::KeyboardMouse);
        CHECK(e[2].route == Route::Off && e[2].conflict);
        CHECK(e[3].route == Route::Off && e[3].conflict);
    }
    // Disconnected explicit gamepad stays assigned but not connected.
    {
        Assignment m; m.port[1] = src("gamepad3");
        auto e = resolve(m, {2, false});
        CHECK(e[1].route == Route::Gamepad && !e[1].connected);
    }
    // Off on port 1 is honoured (spectator-style).
    {
        Assignment m; m.port[0] = src("off");
        auto e = resolve(m, {2, false});
        CHECK(e[0].route == Route::Off && !e[0].connected);
        CHECK(e[1].route == Route::Gamepad && e[1].pad == 1);  // pad1 is free when port 1 is off
    }
    if (failures == 0) std::printf("input assignment contract: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
