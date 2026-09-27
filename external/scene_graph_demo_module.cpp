// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file scene_graph_demo_module.cpp
 * @brief Solar-system showcase for the entity/component scene graph.
 *
 * Builds a node hierarchy and animates only the orbit pivots, letting the scene
 * graph propagate every world transform:
 *
 *   solar_system                  (owns the shared sphere + materials; ambient fill)
 *   ├── sun                       (emissive sphere + point light at the centre)
 *   └── orbit_pivot (per planet)  (spins one way -> the planet revolves)
 *         └── planet              (sphere via mesh_component)
 *               └── moon_pivot    (spins the OTHER way -> retrograde moons)
 *                     └── moon    (smaller sphere)
 *
 * A planet revolves because its orbit pivot turns; a moon revolves because its
 * moon pivot — a child of the planet — turns, and it turns with the opposite
 * sign so moons sweep the other way from the planets. Every pivot carries an
 * @c orbit_pivot behaviour that turns it about the world up axis (+Z), so the
 * system lies flat in the XY plane, the natural orientation for this Z-up
 * engine. Nothing here touches world matrices directly; the camera comes from
 * camera_module (WASD + mouse).
 *
 * The nodes live in the demo's own scene, loaded additively by the bootstrap;
 * the engine unloads it on shutdown, freeing the bodies before the
 * @c solar_system behaviour that owns what they draw with.
 *
 * Lighting: a single point light at the world origin (the sun), so every body
 * is lit on its sun-facing side and dark on the far side, radially correct all
 * the way around its orbit; the sun sphere is emissive so it reads as the
 * source. The point light casts an omni (six-face) shadow map, so a moon goes
 * dark when it passes behind its planet and drops an eclipse shadow onto it.
 */

#include "api/game_module.hpp"

#include <core/math/math.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/assets/tangent.hpp>
#include <rendering_engine/assets/vertex.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/scene_assets.hpp>
#include <runtime/scene_manager.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace math = core::math;

    // Unit sphere as an indexed (stacks + 1) x (slices + 1) lat/lon grid
    // carrying tangents — the record standard_material reads. The normal
    // equals the position and bodies are scaled per node. Two triangles per
    // stack/slice cell, wound CCW when seen from outside.
    rendering_engine::mesh_data make_sphere(int stacks, int slices)
    {
        std::vector<rendering_engine::vertex_position_uv_normal> vertices;
        std::vector<uint32_t> indices;
        const float pi = 3.14159265358979f;

        for (int stack = 0; stack <= stacks; ++stack)
        {
            const float phi = pi * (static_cast<float>(stack) / stacks); // 0..pi from +Z pole
            for (int slice = 0; slice <= slices; ++slice)
            {
                const float theta = 2.0f * pi * (static_cast<float>(slice) / slices);
                const math::vec3 p{std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta), std::cos(phi)};
                const math::vec2 uv{static_cast<float>(slice) / slices, static_cast<float>(stack) / stacks};
                vertices.push_back(rendering_engine::vertex_position_uv_normal{p, uv, p});
            }
        }

        const auto columns = static_cast<uint32_t>(slices + 1);
        for (int stack = 0; stack < stacks; ++stack)
        {
            for (int slice = 0; slice < slices; ++slice)
            {
                const uint32_t a = static_cast<uint32_t>(stack) * columns + static_cast<uint32_t>(slice);
                const uint32_t b = a + columns; // (stack + 1, slice)
                const uint32_t c = b + 1;       // (stack + 1, slice + 1)
                const uint32_t d = a + 1;       // (stack, slice + 1)
                indices.insert(indices.end(), {a, b, c, a, c, d});
            }
        }

        return rendering_engine::mesh_data::from_vertices(rendering_engine::generate_tangents(vertices, indices),
                                                          std::move(indices));
    }

    // The one sphere every body draws, built once and shared through the
    // asset cache; the key encodes the tessellation. A saved scene names the
    // bodies' mesh by this key, and the resolver the bootstrap registers
    // rebuilds it when a load finds it gone from the cache.
    constexpr const char* sphere_key = "scene_graph_demo:sphere:18x36";

    std::shared_ptr<rendering_engine::mesh_asset> shared_sphere()
    {
        return runtime::current_engine().assets->get_or_create_mesh(sphere_key, [] { return make_sphere(18, 36); });
    }

    // The system's root: owns what every body shares — one sphere upload,
    // fetched from the asset cache, and the materials. The bodies are its
    // descendants, so the scene frees them first.
    struct solar_system final : runtime::behavior
    {
        explicit solar_system(std::shared_ptr<rendering_engine::mesh_asset> sphere) : m_sphere{std::move(sphere)} {}

        const std::shared_ptr<rendering_engine::mesh_asset>& sphere() const noexcept
        {
            return m_sphere;
        }

        rendering_engine::standard_material* make_material(const rendering_engine::color& base, float roughness)
        {
            auto material = runtime::current_engine().renderer->create_standard_material();
            material->set_base_color(base);
            material->set_metalness(0.0f);
            material->set_roughness(roughness);
            m_materials.push_back(std::move(material));
            return m_materials.back().get();
        }

    private:
        std::shared_ptr<rendering_engine::mesh_asset> m_sphere;
        std::vector<std::unique_ptr<rendering_engine::standard_material>> m_materials;
    };

    // Turns its node about the world up axis (+Z) from its own clock at a
    // fixed rate; the orbit pivots (planets one way, moons the other) are all
    // just these.
    struct orbit_pivot final : runtime::behavior
    {
        // Saved with the scene: the rate and how far along the orbit it is.
        static void reflect(runtime::type_builder<orbit_pivot>& type)
        {
            type.field("rate", &orbit_pivot::m_rate).field("time", &orbit_pivot::m_time);
        }

        explicit orbit_pivot(float rate) : m_rate{rate} {}

        void on_update(float delta_time) override
        {
            m_time += delta_time / 1000.0f;
            owner().transform.set_rotation(math::vec3{0.0f, 0.0f, m_rate * m_time});
        }

    private:
        float m_rate; // radians per second; sign sets the direction
        float m_time{0.0f};
    };

    // Spawns the bodies of one system into @p scene.
    struct system_builder
    {
        runtime::scene& scene;
        solar_system& system;
        int planet_count{0};

        runtime::node& make_child(runtime::node& parent)
        {
            return scene.create_node({}, &parent);
        }

        // Adds a sphere "visual" leaf under @p parent at @p offset, scaled to
        // @p radius. Scale lives only on these childless leaves: scaling a
        // node scales its whole subtree, so the orbit/anchor nodes that carry
        // children stay unscaled and only the rendered spheres take a size.
        runtime::node& make_visual(runtime::node& parent,
                                   const math::vec3& offset,
                                   float radius,
                                   rendering_engine::standard_material* material)
        {
            runtime::node& visual = make_child(parent);
            visual.transform.set_position(offset);
            visual.transform.set_scale(math::vec3{radius, radius, radius});
            visual.add_component(runtime::mesh_component{material, system.sphere()});
            return visual;
        }

        // Adds a planet: an orbit pivot under @p parent, an unscaled anchor
        // out at @p distance along +X, the planet sphere on the anchor, and
        // @p moons moons on their own retrograde pivots hung off the same
        // anchor (so the planet's scale never reaches them).
        void make_planet(runtime::node& parent,
                         float distance,
                         float radius,
                         float orbit_rate,
                         const rendering_engine::color& color,
                         int moons)
        {
            runtime::node& orbit = make_child(parent);
            runtime::add_behavior<orbit_pivot>(orbit, orbit_rate);

            runtime::node& anchor = make_child(orbit);
            anchor.set_name("planet" + std::to_string(planet_count++)); // reachable via scene.find(...)
            anchor.transform.set_position(math::vec3{distance, 0.0f, 0.0f});

            make_visual(anchor, math::vec3{0.0f, 0.0f, 0.0f}, radius, system.make_material(color, 0.8f));

            auto* moon_material = system.make_material(rendering_engine::color{170, 170, 180, 255}, 0.9f);
            for (int i = 0; i < moons; ++i)
            {
                runtime::node& moon_pivot = make_child(anchor);
                // Retrograde: moons sweep opposite to the planets' orbits, and
                // each moon of a planet runs at its own rate so they do not
                // overlap.
                runtime::add_behavior<orbit_pivot>(moon_pivot, -1.7f - 0.6f * static_cast<float>(i));

                const float moon_distance = radius + 0.45f + 0.35f * static_cast<float>(i);
                make_visual(moon_pivot, math::vec3{moon_distance, 0.0f, 0.0f}, 0.15f, moon_material);
            }
        }
    };
} // namespace

REFLECT_TYPES()
{
    registry.register_behavior<solar_system>("solar_system",
                                             [] { return std::make_unique<solar_system>(shared_sphere()); });
    registry.register_behavior<orbit_pivot>("orbit_pivot", [] { return std::make_unique<orbit_pivot>(0.0f); });
}

GAME_MODULE()
{
    runtime::register_mesh_resolver(sphere_key, [](const std::string&) { return shared_sphere(); });

    runtime::scene& demo = runtime::current_engine().scenes->load("scene_graph_demo", runtime::load_mode::additive);

    // Build the sphere once and share it across every body via the asset
    // cache; the key encodes the tessellation so a second request returns this
    // upload. The asset is indexed, so each mesh_component's model draws it
    // indexed.
    auto sphere = shared_sphere();

    runtime::node& root = demo.create_node("solar_system");
    solar_system* system = runtime::add_behavior<solar_system>(root, std::move(sphere));
    if (system == nullptr)
    {
        return;
    }

    // Dim fill so the night side of each body is not pure black.
    auto ambient = std::make_unique<rendering_engine::ambient_light>();
    ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    ambient->intensity = 0.05f;
    root.add_component(runtime::light_component{std::move(ambient)});

    system_builder builder{demo, *system};

    // The sun is the only direct light: a point light at the world origin, so
    // every body is lit on its sun-facing side and dark on the far side, all
    // the way around its orbit. Constant attenuation (no distance falloff)
    // keeps the outer planets as bright as the inner ones. The sphere is
    // emissive so it reads as the source. cast_shadow turns on the omni
    // (six-face) shadow map, so a moon goes dark behind its planet and casts
    // an eclipse shadow on it.
    auto* sun_material = system->make_material(rendering_engine::color{255, 220, 120, 255}, 1.0f);
    sun_material->set_emissive(rendering_engine::color{255, 210, 110, 255});
    sun_material->set_emissive_intensity(3.0f);

    // The sun is a childless leaf, so giving it a scale is safe; its point
    // light sits at the same node and scale never touches a translation.
    runtime::node& sun = builder.make_visual(root, math::vec3{0.0f, 0.0f, 0.0f}, 1.0f, sun_material);
    sun.set_name("sun");
    auto sun_light = std::make_unique<rendering_engine::point_light>();
    sun_light->color = math::vec3{1.0f, 0.96f, 0.88f};
    sun_light->intensity = 3.0f;
    sun_light->constant_attenuation = 1.0f;
    sun_light->linear_attenuation = 0.0f;
    sun_light->quadratic_attenuation = 0.0f;
    sun_light->cast_shadow = true;
    sun.add_component(runtime::light_component{std::move(sun_light)});

    // Planets: distance, radius, orbit rate (rad/s, all same sign), colour,
    // moon count. Inner planets orbit faster, the classic look.
    builder.make_planet(root, 1.7f, 0.35f, 0.70f, rendering_engine::color{120, 170, 255, 255}, 0);
    builder.make_planet(root, 2.7f, 0.50f, 0.50f, rendering_engine::color{220, 110, 80, 255}, 1);
    builder.make_planet(root, 3.7f, 0.70f, 0.34f, rendering_engine::color{210, 180, 120, 255}, 2);
    builder.make_planet(root, 4.6f, 0.45f, 0.24f, rendering_engine::color{150, 220, 200, 255}, 1);
}
