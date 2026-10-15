#include "idle_entry.hpp"

#include <bit>

namespace rage_wars::player {
namespace {
float read_float(const WordStorage& storage, ObjectId object, std::uint32_t offset) {
    return std::bit_cast<float>(storage.load_word(object, offset));
}
}

void enter_idle_state(WordStorage& storage, PlayerStateTransition& transition,
                      ObjectId actor, std::uint32_t caller_context) {
    const float forward_speed = read_float(storage, actor, 0x6C0);
    if (forward_speed == 0.0F) {
        const float strafe_speed = read_float(storage, actor, 0x6C4);
        if (strafe_speed == 0.0F) return;
    }
    transition.set_state(actor, caller_context, 3);
}

} // namespace rage_wars::player
