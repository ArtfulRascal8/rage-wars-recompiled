#pragma once
// Per-player input assignment: which physical source drives each of the four
// guest controller ports. Pure logic (no SDL, no OpenXR) so it is unit tested.
//
// Defaults reproduce the shipped behaviour exactly: port 1 is the merged
// keyboard + mouse / first gamepad / VR-touch controller, and ports 2-4 are
// extra gamepads in connection order (off when none is connected).
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace xr64::rage_wars::input_assignment {

inline constexpr unsigned kPorts = 4;
inline constexpr unsigned kMaxGamepads = 4;

enum class Kind : std::uint8_t { Auto, KeyboardMouse, Gamepad, Xr, Off };

struct Source {
    Kind kind = Kind::Auto;
    std::uint8_t index = 0;  // gamepad number, 1-based; 0 otherwise
    friend bool operator==(const Source&, const Source&) = default;
};

struct Assignment {
    std::array<Source, kPorts> port{};
    friend bool operator==(const Assignment&, const Assignment&) = default;
};

struct Environment {
    unsigned gamepads_connected = 0;
    bool xr_available = false;
};

// What the host does for a port after resolution.
enum class Route : std::uint8_t {
    Off,            // no controller plugged in
    Legacy,         // today's merged keyboard+mouse / gamepad 1 / VR touch input
    KeyboardMouse,  // keyboard + mouse only
    Gamepad,        // one gamepad (Effective::pad, 1-based)
    Xr,             // VR controllers only
};

struct Effective {
    Route route = Route::Off;
    std::uint8_t pad = 0;
    bool connected = false;
    bool conflict = false;  // the requested source was already used by a lower port
    friend bool operator==(const Effective&, const Effective&) = default;
};

namespace detail {
inline std::string lower(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}
inline bool starts_with(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}
}  // namespace detail

// Accepts: auto, keyboard / kbm / mouse / keyboard+mouse, gamepad / pad / controller
// (optionally followed by 1-4), vr / xr, off / none / disabled.
inline bool parse_source(std::string_view text, Source& out) {
    const std::string value = detail::lower(text);
    if (value.empty()) return false;
    if (value == "auto" || value == "default") { out = {Kind::Auto, 0}; return true; }
    if (value == "off" || value == "none" || value == "disabled" || value == "unplugged") { out = {Kind::Off, 0}; return true; }
    if (value == "vr" || value == "xr" || value == "headset") { out = {Kind::Xr, 0}; return true; }
    if (value == "keyboard" || value == "kbm" || value == "mouse" || value == "keyboard+mouse" || value == "keyboardmouse" ||
            value == "keyboardandmouse") {
        out = {Kind::KeyboardMouse, 0};
        return true;
    }
    for (const char* prefix : {"gamepad", "controller", "pad"}) {
        if (!detail::starts_with(value, prefix)) continue;
        const std::string rest = value.substr(std::char_traits<char>::length(prefix));
        if (rest.empty()) { out = {Kind::Gamepad, 1}; return true; }
        if (rest.size() == 1 && rest[0] >= '1' && rest[0] <= static_cast<char>('0' + kMaxGamepads)) {
            out = {Kind::Gamepad, static_cast<std::uint8_t>(rest[0] - '0')};
            return true;
        }
        return false;
    }
    return false;
}

inline std::string to_string(const Source& source) {
    switch (source.kind) {
    case Kind::Auto: return "auto";
    case Kind::KeyboardMouse: return "keyboard";
    case Kind::Gamepad: return "gamepad" + std::to_string(source.index == 0 ? 1 : source.index);
    case Kind::Xr: return "vr";
    case Kind::Off: return "off";
    }
    return "auto";
}

// Text format, one assignment per line:   player1 = keyboard      (# or ; start a comment)
// Keys: player1..player4 or p1..p4. Unknown keys and bad values are ignored and reported
// through `problems` (the previous value is kept). Returns true when no problem was found.
inline bool parse_config(std::string_view text, Assignment& assignment, unsigned* problems = nullptr) {
    unsigned bad = 0;
    std::size_t position = 0;
    while (position <= text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(position, end - position);
        position = end + 1;
        const auto comment = line.find_first_of("#;");
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            if (!detail::lower(line).empty()) ++bad;
            continue;
        }
        const std::string key = detail::lower(line.substr(0, equals));
        unsigned slot = kPorts;
        if (key.size() == 7 && detail::starts_with(key, "player") && key[6] >= '1' && key[6] <= '4') slot = static_cast<unsigned>(key[6] - '1');
        else if (key.size() == 2 && key[0] == 'p' && key[1] >= '1' && key[1] <= '4') slot = static_cast<unsigned>(key[1] - '1');
        Source parsed;
        if (slot >= kPorts || !parse_source(line.substr(equals + 1), parsed)) { ++bad; continue; }
        assignment.port[slot] = parsed;
    }
    if (problems != nullptr) *problems = bad;
    return bad == 0;
}

inline std::string serialize(const Assignment& assignment) {
    std::string out =
        "# Rage Wars Recompiled - who plays on which controller port.\n"
        "# Values: auto | keyboard | gamepad1..gamepad4 | vr | off\n"
        "# auto: player1 = keyboard+mouse / first gamepad / VR (as always);\n"
        "#       player2-4 = the next connected gamepads, in order.\n";
    for (unsigned i = 0; i < kPorts; ++i) out += "player" + std::to_string(i + 1) + " = " + to_string(assignment.port[i]) + "\n";
    return out;
}

inline Effective gamepad_effective(unsigned pad, const Environment& env) {
    return {Route::Gamepad, static_cast<std::uint8_t>(pad), pad >= 1 && pad <= env.gamepads_connected, false};
}

inline std::array<Effective, kPorts> resolve(const Assignment& assignment, const Environment& env) {
    std::array<Effective, kPorts> result{};
    bool keyboard_taken = false, xr_taken = false;
    std::array<bool, kMaxGamepads + 1> pad_taken{};

    // Pass 1: explicit assignments, lowest port first. A repeated source is a conflict -> off.
    for (unsigned port = 0; port < kPorts; ++port) {
        const Source& want = assignment.port[port];
        Effective& out = result[port];
        switch (want.kind) {
        case Kind::Auto: break;  // pass 2
        case Kind::Off: out = {Route::Off, 0, false, false}; break;
        case Kind::KeyboardMouse:
            if (keyboard_taken) { out = {Route::Off, 0, false, true}; break; }
            keyboard_taken = true;
            out = {Route::KeyboardMouse, 0, true, false};
            break;
        case Kind::Xr:
            if (xr_taken) { out = {Route::Off, 0, false, true}; break; }
            xr_taken = true;
            out = {Route::Xr, 0, env.xr_available, false};
            break;
        case Kind::Gamepad: {
            const unsigned pad = want.index == 0 ? 1U : want.index;
            if (pad > kMaxGamepads || pad_taken[pad]) { out = {Route::Off, 0, false, true}; break; }
            pad_taken[pad] = true;
            out = gamepad_effective(pad, env);
            break;
        }
        }
    }
    // Pass 2: automatic ports. Port 1 keeps the legacy merged input and so owns keyboard+mouse,
    // gamepad 1 and the VR touch controllers unless another port claimed them explicitly.
    unsigned next_pad = 1;
    for (unsigned port = 0; port < kPorts; ++port) {
        if (assignment.port[port].kind != Kind::Auto) continue;
        Effective& out = result[port];
        if (port == 0) {
            out = {Route::Legacy, 0, true, false};
            keyboard_taken = true;
            xr_taken = true;
            pad_taken[1] = true;
            continue;
        }
        while (next_pad <= kMaxGamepads && pad_taken[next_pad]) ++next_pad;
        if (next_pad <= kMaxGamepads && next_pad <= env.gamepads_connected) {
            pad_taken[next_pad] = true;
            out = gamepad_effective(next_pad, env);
            ++next_pad;
        } else {
            out = {Route::Off, 0, false, false};
        }
    }
    return result;
}

// Ports that own the shared sources (for mouse look / VR camera binding). -1 when none.
inline int keyboard_mouse_port(const std::array<Effective, kPorts>& effective) {
    for (unsigned port = 0; port < kPorts; ++port)
        if (effective[port].connected && (effective[port].route == Route::Legacy || effective[port].route == Route::KeyboardMouse)) return static_cast<int>(port);
    return -1;
}
inline int xr_port(const std::array<Effective, kPorts>& effective) {
    for (unsigned port = 0; port < kPorts; ++port)
        if (effective[port].route == Route::Xr || effective[port].route == Route::Legacy) return static_cast<int>(port);
    return -1;
}

}  // namespace xr64::rage_wars::input_assignment
