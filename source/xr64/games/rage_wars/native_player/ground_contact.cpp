#include "ground_contact.hpp"

namespace rage_wars::player {

GroundContactResult ground_contact(const WordStorage& storage, ObjectId object) {
    const auto object_type = storage.load_byte(object, 0);
    if (object_type != 1U) return {object_type, 0};
    return {object_type, storage.load_word(object, 0x38) & 7U};
}

} // namespace rage_wars::player
