#include "player_view.hpp"

namespace rage_wars::player {
namespace {
// Each operation rounds independently to binary32. The library is also built
// with strict FP options, preventing reassociation and fused multiply/add.
float add(float a, float b) { volatile float result = a + b; return result; }
float sub(float a, float b) { volatile float result = a - b; return result; }
float mul(float a, float b) { volatile float result = a * b; return result; }
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
}

void PlayerView::update(ObjectId actor_id, ObjectId source_id) {
    const auto actor = storage_.actor(actor_id);
    const auto source = storage_.actor(source_id);
    // Keep this handle even if a later callback changes the actor's camera.
    const ObjectId camera_id = actor.word(fields::actor::camera);
    if (camera_id != 0) {
        const auto mode = actor.word(fields::actor::camera_mode);
        ViewParameters p;
        bool has_parameters = false;
        if (mode == 1) {
            p.view_type = 0;
            p.rotation_28 = source.word(WordField{fields::common::angle_6c.offset});
            p.rotation_2c = bits(sub(add(actor.scalar(fields::actor::value_724),
                                        actor.scalar(fields::actor::value_730)),
                                    actor.scalar(fields::actor::value_708)));
            p.parameter_30 = add(actor.scalar(fields::actor::value_728), actor.scalar(fields::actor::value_734));
            p.parameter_34 = add(actor.scalar(fields::actor::value_72c), actor.scalar(fields::actor::value_738));
            p.x = add(source.scalar(fields::common::x), actor.scalar(fields::actor::value_73c));
            p.y = source.scalar(fields::common::y);
            p.z = add(source.scalar(fields::common::z), actor.scalar(fields::actor::value_744));
            const auto eye = sub(sub(add(actor.scalar(fields::actor::value_740),
                                          actor.scalar(fields::actor::value_780)),
                                      actor.scalar(fields::actor::value_718)),
                                  actor.scalar(fields::actor::value_720));
            const auto lower = globals_.scalar(ViewGlobal::eye_lower_limit);
            p.eye = eye < lower ? lower : eye;
            p.blend = actor.scalar(fields::actor::blend_7ec);
            p.trailing_60 = globals_.scalar(ViewGlobal::mode1_trailing);
            has_parameters = true;
        } else if (mode == 2) {
            const auto flags = actor.word(fields::actor::input_flags);
            if (flags & 0x200) {
                globals_.scalar(ViewGlobal::yaw_offset, add(globals_.scalar(ViewGlobal::yaw_offset), globals_.scalar(ViewGlobal::yaw_increment)));
            } else if (flags & 0x100) {
                globals_.scalar(ViewGlobal::yaw_offset, sub(globals_.scalar(ViewGlobal::yaw_offset), globals_.scalar(ViewGlobal::yaw_decrement)));
            } else if (flags & 0x800) {
                globals_.scalar(ViewGlobal::distance, sub(globals_.scalar(ViewGlobal::distance), globals_.scalar(ViewGlobal::distance_decrement)));
            } else if (flags & 0x400) {
                globals_.scalar(ViewGlobal::distance, add(globals_.scalar(ViewGlobal::distance), globals_.scalar(ViewGlobal::distance_increment)));
            }
            const auto angle = add(source.scalar(fields::common::angle_6c), globals_.scalar(ViewGlobal::yaw_offset));
            const auto first = callbacks_.first_angle_function(angle);
            p.x = add(source.scalar(fields::common::x), mul(globals_.scalar(ViewGlobal::distance), first));
            const auto second = callbacks_.second_angle_function(angle);
            p.z = add(source.scalar(fields::common::z), mul(globals_.scalar(ViewGlobal::distance), second));
            p.y = add(source.scalar(fields::common::y), globals_.scalar(ViewGlobal::mode2_height));
            p.view_type = 1;
            p.rotation_28 = bits(angle);
            p.rotation_2c = 0;
            p.parameter_30 = p.parameter_34 = p.eye = p.blend = 0.0f;
            p.trailing_60 = globals_.scalar(ViewGlobal::mode2_trailing);
            has_parameters = true;
        }
        if (has_parameters) {
            for (unsigned i = 0; i < 4; ++i) p.orientation[i] = source.word(fields::common::orientation[i]);
            p.region = source.word(fields::common::region);
            commit(camera_id, p);
        }
        callbacks_.update_camera(camera_id);
    }
    callbacks_.update_actor(actor_id);
}

void PlayerView::commit(ObjectId camera_id, ViewParameters p) {
    const auto camera = storage_.object(camera_id);
    camera.word(fields::camera::view_type, p.view_type);
    camera.word(fields::camera::rotation_28, p.rotation_28);
    camera.word(fields::camera::rotation_2c, p.rotation_2c);
    camera.scalar(fields::camera::parameter_30, p.parameter_30);
    camera.scalar(fields::camera::parameter_34, p.parameter_34);
    camera.scalar(fields::camera::x, p.x);
    camera.scalar(fields::camera::y, p.y);
    camera.scalar(fields::camera::z, p.z);
    camera.scalar(fields::camera::eye, p.eye);
    for (unsigned i = 0; i < 4; ++i) camera.word(fields::camera::orientation[i], p.orientation[i]);
    camera.word(fields::camera::region, p.region);
    camera.scalar(fields::camera::blend, p.blend);
    camera.scalar(fields::camera::trailing_60, p.trailing_60);

    if (callbacks_.state_query(StateQuery::before_region_lookup)) {
        camera.word(fields::camera::region, callbacks_.locate_region(camera_id));
    }
    const auto default_visibility = globals_.load_word(ViewGlobal::default_visibility);
    const ObjectId selected_region = camera.word(fields::camera::region);
    const auto x = camera.scalar(fields::camera::x);
    const auto y = camera.scalar(fields::camera::y);
    const auto z = camera.scalar(fields::camera::z);
    const auto eye = camera.scalar(fields::camera::eye);
    camera.word(fields::camera::visibility, default_visibility);
    if (selected_region != 0 && !callbacks_.state_query(StateQuery::before_height)) {
        const auto height = callbacks_.region_height(selected_region, x, z);
        const auto delta = mul(sub(add(y, eye), height), globals_.scalar(ViewGlobal::height_scale));
        if (delta < globals_.scalar(ViewGlobal::height_upper)
                && globals_.scalar(ViewGlobal::height_lower) < delta) {
            camera.word(fields::camera::visibility,
                        storage_.object(selected_region).word(fields::region::visibility));
        }
    }
    if (callbacks_.state_query(StateQuery::final_visibility)) {
        camera.word(fields::camera::visibility, globals_.load_word(ViewGlobal::default_visibility));
    }
    if (camera.scalar(fields::camera::blend) > 0.0f) {
        camera.word(fields::camera::visibility, globals_.load_word(ViewGlobal::default_visibility));
    }
}

} // namespace rage_wars::player