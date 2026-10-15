#include "movement_speed.hpp"

#include "ground_contact.hpp"

#include <bit>

namespace rage_wars::player {
namespace {
float read_float(const WordStorage& storage, ObjectId object, std::uint32_t offset) {
    return std::bit_cast<float>(storage.load_word(object, offset));
}
void write_float(WordStorage& storage, ObjectId object, std::uint32_t offset, float value) {
    storage.store_word(object, offset, std::bit_cast<std::uint32_t>(value));
}
float rounded_multiply(float a, float b) {
    volatile float result = a * b;
    return result;
}
void multiply_speed(WordStorage& storage, ObjectId actor, float factor) {
    const float current = read_float(storage, actor, 0x784);
    write_float(storage, actor, 0x784, rounded_multiply(current, factor));
}
}

void update_movement_speed(WordStorage& storage, ObjectId actor,
                           ObjectId movement_source, ObjectId state_record) {
    const auto state = storage.load_halfword(actor, 0x650);
    if (state < 2U) {
        write_float(storage, actor, 0x784, read_float(storage, 0x800C2778U, 0));
        return;
    }

    const auto definition = storage.load_word(movement_source, 0x18);
    const float base_speed = read_float(storage, definition, 0x1C);
    write_float(storage, actor, 0x784, base_speed);

    if (storage.load_word(actor, 0x1450) != 0) {
        const auto profile = storage.load_word(actor, 0x5D8);
        auto speed_class = storage.load_byte(profile, 0x93);
        if (speed_class == 1U) {
            const float factor = read_float(storage, 0x800C2780U, 0);
            write_float(storage, actor, 0x784, rounded_multiply(base_speed, factor));
        } else if (speed_class == 0U || speed_class > 2U) {
            if (speed_class > 2U) storage.store_byte(profile, 0x93, 0);
            const float current = read_float(storage, actor, 0x784);
            const float factor = read_float(storage, 0x800C277CU, 0);
            write_float(storage, actor, 0x784, rounded_multiply(current, factor));
        }

        if (storage.load_word(actor, 0x1450) != 0) {
            const auto second_profile = storage.load_word(actor, 0x5D8);
            speed_class = storage.load_byte(second_profile, 0x93);
            if (speed_class == 1U) {
                const float current = read_float(storage, actor, 0x784);
                const float factor = read_float(storage, 0x800C2788U, 0);
                write_float(storage, actor, 0x784, rounded_multiply(current, factor));
            } else if (speed_class == 0U || speed_class > 2U) {
                if (speed_class > 2U) storage.store_byte(second_profile, 0x93, 0);
                const float current = read_float(storage, actor, 0x784);
                const float factor = read_float(storage, 0x800C2784U, 0);
                write_float(storage, actor, 0x784, rounded_multiply(current, factor));
            }
        }
    }

    const ObjectId game_config = 0x80140208U;
    if (storage.load_byte(game_config, 0x1D) != 0) {
        const float current = read_float(storage, actor, 0x784);
        const float factor = read_float(storage, game_config, 0x20);
        write_float(storage, actor, 0x784, rounded_multiply(current, factor));
    }

    if ((storage.load_word(movement_source, 0x38) & 3U) == 0 && state_record != 0) {
        const auto contact = ground_contact(storage, movement_source);
        if (contact.contact_code != 0 || (storage.load_halfword(state_record, 0x52) & 0x800U) != 0) {
            const float current = read_float(storage, actor, 0x784);
            const float factor = read_float(storage, state_record, 0x30);
            write_float(storage, actor, 0x784, rounded_multiply(current, factor));
        }
    }
}

} // namespace rage_wars::player
