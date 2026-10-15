#pragma once

#include "actor_storage.hpp"
#include <array>

namespace rage_wars::player {

// Names identify the recovered role or offset; unnamed scalar semantics remain
// provisional. Values are read live at the original callback boundaries.
enum class ViewGlobal {
    eye_lower_limit, mode1_trailing, yaw_increment, yaw_decrement,
    distance_decrement, distance_increment, mode2_height, mode2_trailing,
    height_scale, height_upper, height_lower, yaw_offset, distance,
    default_visibility
};
class ViewGlobals {
public:
    virtual ~ViewGlobals() = default;
    [[nodiscard]] virtual std::uint32_t load_word(ViewGlobal) const = 0;
    virtual void store_word(ViewGlobal, std::uint32_t) = 0;
    [[nodiscard]] float scalar(ViewGlobal value) const {
        return std::bit_cast<float>(load_word(value));
    }
    void scalar(ViewGlobal value, float number) {
        store_word(value, std::bit_cast<std::uint32_t>(number));
    }
};

enum class StateQuery { before_region_lookup, before_height, final_visibility };
class ViewCallbacks {
public:
    virtual ~ViewCallbacks() = default;
    [[nodiscard]] virtual bool state_query(StateQuery) = 0;
    // Position is the live camera+0x38 field, not a copied vector. Callbacks may
    // mutate storage or globals; subsequent loads must observe those mutations.
    [[nodiscard]] virtual ObjectId locate_region(ObjectId camera) = 0;
    [[nodiscard]] virtual float region_height(ObjectId region, float x, float z) = 0;
    [[nodiscard]] virtual float first_angle_function(float) = 0;
    [[nodiscard]] virtual float second_angle_function(float) = 0;
    virtual void update_camera(ObjectId) = 0;
    virtual void update_actor(ObjectId) = 0;
};

struct ViewParameters {
    std::uint32_t view_type{};
    std::uint32_t rotation_28{}, rotation_2c{};
    float parameter_30{}, parameter_34{}, x{}, y{}, z{}, eye{};
    std::array<std::uint32_t, 4> orientation{};
    ObjectId region{};
    float blend{}, trailing_60{};
};

// Original-rate behavior only. Scheduling, interpolation, graphics submission,
// flythrough selection, and XR pose composition remain with their owners.
class PlayerView {
public:
    PlayerView(RageWarsActorStorage& storage, ViewGlobals& globals, ViewCallbacks& callbacks)
        : storage_(storage), globals_(globals), callbacks_(callbacks) {}
    void update(ObjectId actor, ObjectId source);
    void commit(ObjectId camera, ViewParameters);
private:
    RageWarsActorStorage& storage_;
    ViewGlobals& globals_;
    ViewCallbacks& callbacks_;
};

} // namespace rage_wars::player