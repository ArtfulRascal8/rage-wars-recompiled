#include "damage_descriptor.hpp"

namespace rage_wars::player {

void write_damage_descriptor(WordStorage& storage, ObjectId address,
                              const DamageDescriptorWords& words) {
    for (std::uint32_t index = 0; index < words.size(); ++index) {
        storage.store_word(address, index * 4U, words[index]);
    }
}

} // namespace rage_wars::player
