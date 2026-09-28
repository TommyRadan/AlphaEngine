// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <assets/color.hpp>
#include <assets/mesh_generators.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>

#include <memory>
#include <utility>

// A row of spheres marching away from the camera over a long ground
// plane, with linear distance fog: the near spheres read at full colour
// while the far ones dissolve into the haze. Every object is a node under
// one "fog_demo" node, whose behaviour turns the fog on while the showcase
// is enabled.

namespace
{
    // Linear RGB the scene fades into; matched to the fog colour so the
    // receding plane and spheres melt into a uniform haze.
    constexpr assets::color fog_color{90, 115, 160, 255};

    // Holds the scene-wide fog on while its node is enabled, and clears it
    // when the node is disabled or destroyed.
    struct fog_zone final : runtime::behavior
    {
        void on_enable() override
        {
            // Linear distance fog: clear up close, fully saturated by the
            // far end of the sphere row.
            rendering_engine::fog_settings fog{};
            fog.mode = rendering_engine::fog_mode::linear;
            fog.color = core::math::vec3{static_cast<float>(fog_color.r) / 255.0f,
                                         static_cast<float>(fog_color.g) / 255.0f,
                                         static_cast<float>(fog_color.b) / 255.0f};
            fog.near_distance = 3.0f;
            fog.far_distance = 18.0f;
            runtime::current_engine().renderer->set_fog(fog);
        }

        void on_disable() override
        {
            runtime::current_engine().renderer->set_fog(rendering_engine::fog_settings{});
        }
    };

    // Hangs @p mesh, drawn with @p material, on a new child of @p parent at
    // @p position.
    void spawn_prop(runtime::scene& scene,
                    runtime::node& parent,
                    const core::math::vec3& position,
                    rendering_engine::material* material,
                    std::shared_ptr<rendering_engine::mesh_asset> mesh)
    {
        runtime::node& prop = scene.create_node({}, &parent);
        prop.transform.set_position(position);
        prop.add_component(runtime::mesh_component{material, std::move(mesh)});
    }
} // namespace

GAME_MODULE()
{
    auto& material = runtime::current_engine().renderer->get_phong_material();
    auto& cache = *runtime::current_engine().assets;
    material.set_diffuse(assets::color{230, 126, 34, 255});
    material.set_specular(assets::color{255, 255, 255, 255});
    material.set_shininess(48.0f);

    runtime::node& demo = scene.create_node("fog_demo");
    runtime::add_behavior<fog_zone>(demo);

    // The camera looks from -X toward the origin. Lay the spheres out as
    // a field that both recedes (+X, increasing distance) and spreads
    // laterally (±Y) so the rows behind the front one stay visible: the
    // near rows read at full colour while the far rows dissolve into the
    // fog. (A single line of spheres along the view axis would hide the
    // fogged rows behind the unfogged front sphere.)
    constexpr int depth_rows = 6;   // along +X, away from the camera
    constexpr int lateral_cols = 5; // across ±Y
    for (int row = 0; row < depth_rows; ++row)
    {
        for (int col = 0; col < lateral_cols; ++col)
        {
            const float x = static_cast<float>(row) * 4.0f;
            const float y = (static_cast<float>(col) - static_cast<float>(lateral_cols - 1) * 0.5f) * 3.0f;
            spawn_prop(scene,
                       demo,
                       core::math::vec3{x, y, 0.0f},
                       &material,
                       cache.get_or_create_mesh(assets::mesh_generators::sphere{}));
        }
    }

    // A long ground plane below the spheres; world up is +Z, so the
    // plane's default +Z normal already faces the sky.
    spawn_prop(scene,
               demo,
               core::math::vec3{16.0f, 0.0f, -1.5f},
               &material,
               cache.get_or_create_mesh(assets::mesh_generators::plane{.width = 120.0f, .height = 120.0f}));

    // The lights are components on nodes of their own; the light component
    // keeps the sun's direction on its node's forward (+X) axis.
    auto ambient = std::make_unique<rendering_engine::ambient_light>();
    ambient->color = core::math::vec3{1.0f, 1.0f, 1.0f};
    ambient->intensity = 0.2f;
    scene.create_node("ambient", &demo).add_component(runtime::light_component{std::move(ambient)});

    auto sun_light = std::make_unique<rendering_engine::directional_light>();
    sun_light->color = core::math::vec3{1.0f, 0.97f, 0.9f};
    sun_light->intensity = 1.0f;
    runtime::node& sun = scene.create_node("sun", &demo);
    sun.look_at(sun.world_position() + core::math::vec3{1.0f, -0.4f, -0.6f});
    sun.add_component(runtime::light_component{std::move(sun_light)});
}
