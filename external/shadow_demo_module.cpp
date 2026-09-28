// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <assets/color.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/renderables/premade_3d/box.hpp>
#include <rendering_engine/renderables/premade_3d/plane.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/scripting/lua_behavior.hpp>

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

// Shadow showcase for the auto-fit directional shadow frustum: a field of
// spheres and tall pillars sits on a wide ground plane, kept within the
// shadow distance so the whole field stays crisp.
//
//   * every object casts a sharp shadow wherever it sits on the plane;
//   * the pillars cast long thin shadows whose edges stay smooth instead
//     of stair-stepping, because the 4096 map's texels land on the visible
//     region rather than being spread over empty world space;
//   * the sun slowly orbits, so the shadows sweep across the plane — watch
//     that the edges stay stable (no crawling) thanks to the texel-snapped,
//     bounding-sphere fit.
//
// This is a single cascade, so the crispness holds out to the shadow
// distance (shadow_pass.cpp) and then stops.
//
// Controls (from camera_module): WASD to move, hold left mouse to look,
// space/ctrl to rise/sink, shift to move faster. Roam around — the
// shadows stay sharp wherever you look.
//
// The scene it builds, all in the scene the engine hands the bootstrap:
//
//   shadow_demo     (shadow_field: the materials; the ambient light)
//   ├── ground      (plane)
//   ├── 15 spheres  (sphere each)
//   ├── 3 pillars   (box each; the first also runs scripts/bob.lua, a Lua
//   │                script under the content root, so it rises, sinks and turns)
//   └── sun         (orbiting_sun: the shadow-casting directional light)

namespace
{
    namespace math = core::math;

    // Top of the ground plane in world Z (world up is +Z). Objects rest on it.
    constexpr float ground_z = -1.5f;

    // The sun slowly orbits in azimuth so the shadows sweep across the plane;
    // the downward tilt is held constant so they never grow unbounded.
    constexpr float sun_orbit_speed = 0.25f; // radians / second
    constexpr float sun_tilt = -0.65f;       // downward (-Z) component

    // The showcase's root: owns the materials its props draw with. The props
    // are its children, so the scene frees them before it.
    struct shadow_field final : runtime::behavior
    {
        rendering_engine::standard_material* make_material(const assets::color& base, float roughness)
        {
            auto material = runtime::current_engine().renderer->create_standard_material();
            material->set_base_color(base);
            material->set_metalness(0.0f);
            material->set_roughness(roughness);
            m_materials.push_back(std::move(material));
            return m_materials.back().get();
        }

    private:
        std::vector<std::unique_ptr<rendering_engine::standard_material>> m_materials;
    };

    // The single shadow-casting directional light, orbiting. Its frustum is
    // auto-fitted to the camera view every frame (see shadow_pass). It lights
    // the scene only while its node is enabled.
    struct orbiting_sun final : runtime::behavior
    {
        // Saved with the scene: the orbit's rate and where along it the sun is.
        static void reflect(runtime::type_builder<orbiting_sun>& type)
        {
            type.field("speed", &orbiting_sun::m_speed).field("angle", &orbiting_sun::m_angle);
        }

        orbiting_sun() : m_light{std::make_unique<rendering_engine::directional_light>()}
        {
            m_light->color = math::vec3{1.0f, 0.97f, 0.9f};
            m_light->intensity = 1.0f;
            m_light->cast_shadow = true;
            m_light->set_enabled(false);
            aim();
        }

        void on_enable() override
        {
            m_light->set_enabled(true);
        }

        void on_disable() override
        {
            m_light->set_enabled(false);
        }

        void on_update(float delta_time) override
        {
            m_angle += m_speed * (delta_time / 1000.0f);
            aim();
        }

    private:
        void aim()
        {
            m_light->direction = math::vec3{std::cos(m_angle) * 0.7f, std::sin(m_angle) * 0.7f, sun_tilt};
        }

        std::unique_ptr<rendering_engine::directional_light> m_light;
        float m_speed{sun_orbit_speed}; // radians / second
        float m_angle{0.0f};
    };

    // Uploads @p shape and hangs it on a new child of @p parent at @p position.
    template<typename Shape>
    runtime::node&
    spawn_prop(runtime::scene& scene, runtime::node& parent, const math::vec3& position, std::unique_ptr<Shape> shape)
    {
        shape->upload();
        runtime::node& prop = scene.create_node({}, &parent);
        prop.transform.set_position(position);
        prop.add_component(runtime::renderable_component{std::move(shape)});
        return prop;
    }
} // namespace

REFLECT_TYPES()
{
    registry.register_behavior<shadow_field>("shadow_field");
    registry.register_behavior<orbiting_sun>("orbiting_sun");
}

GAME_MODULE()
{
    runtime::node& demo = scene.create_node("shadow_demo");
    shadow_field* field = runtime::add_behavior<shadow_field>(demo);
    if (field == nullptr)
    {
        return;
    }
    auto& cache = *runtime::current_engine().assets;

    // Dim ambient so the shadowed areas read as genuinely dark.
    auto ambient = std::make_unique<rendering_engine::ambient_light>();
    ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    ambient->intensity = 0.12f;
    demo.add_component(runtime::light_component{std::move(ambient)});

    // A wide, neutral ground plane to catch the shadows. World up is +Z,
    // so the plane's default +Z normal already faces the sky.
    rendering_engine::standard_material* ground_material =
        field->make_material(assets::color{185, 188, 195, 255}, 0.95f);
    spawn_prop(scene,
               demo,
               math::vec3{6.0f, 0.0f, ground_z},
               std::make_unique<rendering_engine::plane>(cache, ground_material, 60.0f, 60.0f));

    // A few coloured surfaces shared across the field.
    rendering_engine::standard_material* warm = field->make_material(assets::color{230, 126, 34, 255}, 0.55f);
    rendering_engine::standard_material* cool = field->make_material(assets::color{52, 152, 219, 255}, 0.4f);
    rendering_engine::standard_material* pale = field->make_material(assets::color{236, 240, 241, 255}, 0.7f);
    rendering_engine::standard_material* pillar_material =
        field->make_material(assets::color{120, 200, 140, 255}, 0.6f);

    // A grid of unit spheres spread across the plane. The camera looks
    // from -X, so the grid recedes along +X and spreads across +/-Y. It is
    // kept inside the shadow distance so the whole field stays crisp.
    constexpr int depth_rows = 3;   // along +X, away from the camera
    constexpr int lateral_cols = 5; // across +/-Y
    rendering_engine::standard_material* tints[] = {warm, cool, pale};
    for (int row = 0; row < depth_rows; ++row)
    {
        for (int col = 0; col < lateral_cols; ++col)
        {
            rendering_engine::standard_material* tint = tints[(row + col) % 3];
            const float x = static_cast<float>(row) * 5.0f;
            const float y = (static_cast<float>(col) - static_cast<float>(lateral_cols - 1) * 0.5f) * 4.0f;
            // Unit sphere (radius 1): centre one radius above the plane so it
            // rests on the ground and casts a contact shadow.
            spawn_prop(scene,
                       demo,
                       math::vec3{x, y, ground_z + 1.0f},
                       std::make_unique<rendering_engine::sphere>(cache, tint));
        }
    }

    // Tall, thin pillars cast long shadows across the plane — the case
    // where stair-stepping is most obvious, so the smooth edges are the
    // clearest evidence the auto-fit is working.
    const math::vec3 pillar_spots[] = {
        math::vec3{2.0f, -8.0f, 0.0f},
        math::vec3{8.0f, 7.0f, 0.0f},
        math::vec3{11.0f, -3.0f, 0.0f},
    };
    constexpr float pillar_height = 5.0f;
    runtime::node* scripted_pillar = nullptr;
    for (const math::vec3& spot : pillar_spots)
    {
        runtime::node& pillar =
            spawn_prop(scene,
                       demo,
                       math::vec3{spot.x, spot.y, ground_z + pillar_height * 0.5f},
                       std::make_unique<rendering_engine::box>(cache, pillar_material, 0.8f, 0.8f, pillar_height));
        if (scripted_pillar == nullptr)
        {
            scripted_pillar = &pillar;
        }
    }
    // The first pillar's logic is a Lua script (runtime/scripting).
    runtime::add_behavior<runtime::lua_behavior>(*scripted_pillar, "scripts/bob.lua");

    runtime::node& sun = scene.create_node("sun", &demo);
    runtime::add_behavior<orbiting_sun>(sun);
}
