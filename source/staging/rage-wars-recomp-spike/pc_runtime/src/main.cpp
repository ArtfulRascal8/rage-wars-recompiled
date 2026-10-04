#include <librecomp/game.hpp>
#include <ultramodern/ultramodern.hpp>

#include <cstdint>

int main(int argc, char **) {
    // Keep references to symbols from both base libraries. The condition is
    // deliberately unreachable in the Gate 1 link probe, so it validates the
    // ABI and link surface without starting a runtime or touching the ROM.
    if (argc < 0) {
        const recomp::Version &version = recomp::get_project_version();
        return version.major + static_cast<int>(ultramodern::get_display_refresh_rate());
    }
    return 0;
}
