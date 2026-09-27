// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <core/math/math.hpp>
#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/renderables/premade_3d/plane.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/engine.hpp>

#include <memory>
#include <utility>

// A Blinn-Phong lit sphere turning above a ground plane, under an ambient
// fill, a shadow-casting sun and a cool point light. Every object is a node
// in the scene the engine hands the bootstrap, under one "phong_demo" node.

namespace
{
    // Turns its node about the world up axis (+Z) at a constant rate.
    struct turntable final : runtime::behavior
    {
        void on_update(float delta_time) override
        {
            m_angle += rotation_speed * (delta_time / 1000.0f);
            owner().transform.set_rotation(core::math::vec3{0.0f, 0.0f, m_angle});
        }

    private:
        static constexpr float rotation_speed = 3.14f / 4; // radians / second

        float m_angle{0.0f};
    };

    // A child of @p parent carrying the light @p light. The light component
    // keeps a directional light's direction on the node's forward (+X) axis
    // and a point light's position on the node's, so the node is what places
    // it.
    runtime::node&
    spawn_light(runtime::scene& scene, runtime::node& parent, std::unique_ptr<rendering_engine::light> light)
    {
        runtime::node& holder = scene.create_node({}, &parent);
        holder.add_component(runtime::light_component{std::move(light)});
        return holder;
    }
} // namespace

GAME_MODULE()
{
    auto& material = runtime::current_engine().renderer->get_phong_material();
    material.set_diffuse(rendering_engine::color{230, 126, 34, 255});
    material.set_specular(rendering_engine::color{255, 255, 255, 255});
    material.set_shininess(48.0f);

    runtime::node& demo = scene.create_node("phong_demo");

    auto ball = std::make_unique<rendering_engine::sphere>(&material);
    ball->upload();
    runtime::node& sphere = scene.create_node("sphere", &demo);
    sphere.add_component(runtime::renderable_component{std::move(ball)});
    runtime::add_behavior<turntable>(sphere);

    // A large ground plane below the sphere to catch its shadow. World
    // up is +Z here, so the plane's default +Z normal already faces the
    // sky; drop it just under the unit sphere and scale it out.
    auto plane = std::make_unique<rendering_engine::plane>(&material, 30.0f, 30.0f);
    plane->upload();
    runtime::node& ground = scene.create_node("ground", &demo);
    ground.transform.set_position(core::math::vec3{0.0f, 0.0f, -1.5f});
    ground.add_component(runtime::renderable_component{std::move(plane)});

    auto ambient = std::make_unique<rendering_engine::ambient_light>();
    ambient->color = core::math::vec3{1.0f, 1.0f, 1.0f};
    ambient->intensity = 0.15f;
    spawn_light(scene, demo, std::move(ambient));

    // Camera looks from -X toward the origin, so a sun travelling +X
    // (and slightly down) lights the camera-facing hemisphere. It casts
    // the scene's single shadow map onto the ground plane.
    auto sun_light = std::make_unique<rendering_engine::directional_light>();
    sun_light->color = core::math::vec3{1.0f, 0.97f, 0.9f};
    sun_light->intensity = 1.0f;
    sun_light->cast_shadow = true;
    runtime::node& sun = spawn_light(scene, demo, std::move(sun_light));
    sun.look_at(sun.world_position() + core::math::vec3{1.0f, -0.4f, -0.6f});

    // A cool point light off to the camera side for a coloured highlight
    // that falls off with distance.
    auto lamp_light = std::make_unique<rendering_engine::point_light>();
    lamp_light->color = core::math::vec3{0.4f, 0.6f, 1.0f};
    lamp_light->intensity = 25.0f;
    runtime::node& lamp = spawn_light(scene, demo, std::move(lamp_light));
    lamp.transform.set_position(core::math::vec3{-3.0f, 2.0f, 1.5f});
}
