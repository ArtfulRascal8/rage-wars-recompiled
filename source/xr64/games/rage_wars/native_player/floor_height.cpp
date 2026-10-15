#include "floor_height.hpp"

#include <bit>

namespace rage_wars::player {
namespace {
float rounded_subtract(float a, float b) {
    volatile float result = a - b;
    return result;
}
float rounded_multiply(float a, float b) {
    volatile float result = a * b;
    return result;
}
float rounded_add(float a, float b) {
    volatile float result = a + b;
    return result;
}
}

ObjectId update_floor_height(WordStorage& storage, FloorObjectLookup& lookup,
                             ObjectId actor, ObjectId query_object) {
    const ObjectId floor = lookup.find_floor(query_object);
    if (floor == 0) return 0;

    const float target = std::bit_cast<float>(storage.load_word(floor, 0x2C));
    const float current = std::bit_cast<float>(storage.load_word(actor, 0x780));
    const float difference = rounded_subtract(target, current);
    const float factor = std::bit_cast<float>(storage.load_word(0x800C2C58U, 0));
    const float step = rounded_multiply(difference, factor);
    storage.store_word(actor, 0x780, std::bit_cast<std::uint32_t>(rounded_add(current, step)));
    return floor;
}

} // namespace rage_wars::player
