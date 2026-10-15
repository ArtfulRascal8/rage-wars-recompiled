#include "ease_toward.hpp"

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

void ease_toward(WordStorage& storage, ObjectId address, float target, float rate,
                 float snap_rate, float frame_time) {
    const auto value = ObjectView(storage, address);
    const FloatField field{0};
    const float current = value.scalar(field);
    const float difference = rounded_subtract(target, current);
    const float rate_step = rounded_multiply(difference, rate);
    const float step = rounded_multiply(rate_step, frame_time);

    if (rate == snap_rate) {
        value.scalar(field, target);
        return;
    }

    if (current < target) {
        const float updated = rounded_add(current, step);
        value.scalar(field, updated);
        if (target < updated) value.scalar(field, target);
        return;
    }

    if (target < current) {
        const float updated = rounded_add(current, step);
        value.scalar(field, updated);
        if (updated < target) value.scalar(field, target);
    }
}

} // namespace rage_wars::player
