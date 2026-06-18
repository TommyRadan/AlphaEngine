/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file scene_graph_demo_module.cpp
 * @brief Solar-system showcase for the entity/component scene graph.
 *
 * Builds a node hierarchy and animates only the orbit pivots, letting the scene
 * graph propagate every world transform:
 *
 *   root
 *   ├── sun                       (emissive sphere + point light at the centre)
 *   └── orbit_pivot (per planet)  (spins one way -> the planet revolves)
 *         └── planet              (sphere via mesh_component)
 *               └── moon_pivot    (spins the OTHER way -> retrograde moons)
 *                     └── moon    (smaller sphere)
 *
 * A planet revolves because its orbit pivot turns; a moon revolves because its
 * moon pivot — a child of the planet — turns, and it turns with the opposite
 * sign so moons sweep the other way from the planets. Pivots turn about the
 * world up axis (+Z), so the system lies flat in the XY plane, the natural
 * orientation for this Z-up engine. Nothing here touches world matrices
 * directly; the camera comes from camera_module (WASD + mouse).
 *
 * Lighting: a single point light at the world origin (the sun), so every body
 * is lit on its sun-facing side and dark on the far side, radially correct all
 * the way around its orbit; the sun sphere is emissive so it reads as the
 * source. The point light casts an omni (six-face) shadow map, so a moon goes
 * dark when it passes behind its planet and drops an eclipse shadow onto it.
 */

#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/scene_graph.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace math = core::math;

// A node that turns about the world X axis at a fixed rate; the orbit pivots
// (planets one way, moons the other) are all just spinners.
struct spinner
{
    runtime::node* node;
    float rate; // radians per second; sign sets the direction
};

// Everything is owned here — the scene-graph root only holds non-owning links,
// and materials must not outlive the renderer.
static std::vector<std::unique_ptr<runtime::node>> g_nodes;
static std::vector<std::unique_ptr<rendering_engine::standard_material>> g_materials;
static std::vector<spinner> g_spinners;
static std::unique_ptr<rendering_engine::ambient_light> g_ambient;
// One sphere upload shared by every body in the system, fetched from the asset
// cache; each visual references it instead of uploading its own copy.
static std::shared_ptr<rendering_engine::mesh_asset> g_sphere;
static float g_time = 0.0f;

static std::vector<rendering_engine::vertex_position_uv_normal> make_sphere(int stacks, int slices)
{
    std::vector<rendering_engine::vertex_position_uv_normal> v;
    const float pi = 3.14159265358979f;

    // Unit sphere; the normal equals the position and bodies are scaled per
    // node. Two triangles per stack/slice cell, wound CCW when seen from
    // outside.
    auto at = [&](int stack, int slice)
    {
        const float phi = pi * (static_cast<float>(stack) / stacks); // 0..pi from +Z pole
        const float theta = 2.0f * pi * (static_cast<float>(slice) / slices);
        const math::vec3 p{std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta), std::cos(phi)};
        const math::vec2 uv{static_cast<float>(slice) / slices, static_cast<float>(stack) / stacks};
        return rendering_engine::vertex_position_uv_normal{p, uv, p};
    };

    for (int stack = 0; stack < stacks; ++stack)
    {
        for (int slice = 0; slice < slices; ++slice)
        {
            const auto a = at(stack, slice);
            const auto b = at(stack + 1, slice);
            const auto c = at(stack + 1, slice + 1);
            const auto d = at(stack, slice + 1);
            v.push_back(a);
            v.push_back(b);
            v.push_back(c);
            v.push_back(a);
            v.push_back(c);
            v.push_back(d);
        }
    }

    return v;
}

static runtime::node* make_child(runtime::node& parent)
{
    g_nodes.push_back(std::make_unique<runtime::node>());
    runtime::node* node = g_nodes.back().get();
    parent.add(*node);
    return node;
}

static rendering_engine::standard_material* make_material(const rendering_engine::util::color& base, float roughness)
{
    auto material = runtime::current_engine().renderer->create_standard_material();
    material->set_base_color(base);
    material->set_metalness(0.0f);
    material->set_roughness(roughness);
    g_materials.push_back(std::move(material));
    return g_materials.back().get();
}

// Adds a sphere "visual" leaf under @p parent at @p offset, scaled to @p radius.
// Scale lives only on these childless leaves: scaling a node scales its whole
// subtree, so the orbit/anchor nodes that carry children stay unscaled and only
// the rendered spheres take a size.
static runtime::node* make_visual(runtime::node& parent,
                                  const math::vec3& offset,
                                  float radius,
                                  rendering_engine::standard_material* material)
{
    runtime::node* visual = make_child(parent);
    visual->transform.set_position(offset);
    visual->transform.set_scale(math::vec3{radius, radius, radius});
    visual->add_component<runtime::mesh_component>(runtime::mesh_component{material, g_sphere});
    return visual;
}

// Adds a planet: an orbit pivot (spinner about X) under @p parent, an unscaled
// anchor out at @p distance along +Y, the planet sphere on the anchor, and
// @p moons moons on their own retrograde pivots hung off the same anchor (so
// the planet's scale never reaches them).
static void make_planet(runtime::node& parent,
                        float distance,
                        float radius,
                        float orbit_rate,
                        const rendering_engine::util::color& color,
                        int moons)
{
    static int planet_no = 0;

    runtime::node* orbit = make_child(parent);
    g_spinners.push_back(spinner{orbit, orbit_rate});

    runtime::node* anchor = make_child(*orbit);
    anchor->name = "planet" + std::to_string(planet_no++); // reachable via scenes->root.find(...)
    anchor->transform.set_position(math::vec3{distance, 0.0f, 0.0f});

    make_visual(*anchor, math::vec3{0.0f, 0.0f, 0.0f}, radius, make_material(color, 0.8f));

    auto* moon_material = make_material(rendering_engine::util::color{170, 170, 180, 255}, 0.9f);
    for (int i = 0; i < moons; ++i)
    {
        runtime::node* moon_pivot = make_child(*anchor);
        // Retrograde: moons sweep opposite to the planets' orbits, and each
        // moon of a planet runs at its own rate so they do not overlap.
        g_spinners.push_back(spinner{moon_pivot, -1.7f - 0.6f * static_cast<float>(i)});

        const float moon_distance = radius + 0.45f + 0.35f * static_cast<float>(i);
        make_visual(*moon_pivot, math::vec3{moon_distance, 0.0f, 0.0f}, 0.15f, moon_material);
    }
}

static void on_engine_start(const core::engine_start& event)
{
    (void)event;

    // Build the sphere once and share it across every body via the asset cache;
    // the key encodes the tessellation so a second request returns this upload.
    g_sphere = runtime::current_engine().assets->get_or_create_mesh(
        "scene_graph_demo:sphere:18x36",
        [] { return rendering_engine::mesh_data::from_vertices(make_sphere(18, 36)); });

    // Dim fill so the night side of each body is not pure black.
    g_ambient = std::make_unique<rendering_engine::ambient_light>();
    g_ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    g_ambient->intensity = 0.05f;

    runtime::node& root = runtime::current_engine().scenes->root;

    // The sun is the only light: a point light at the world origin, so every
    // body is lit on its sun-facing side and dark on the far side, all the way
    // around its orbit. Constant attenuation (no distance falloff) keeps the
    // outer planets as bright as the inner ones. The sphere is emissive so it
    // reads as the source. cast_shadow turns on the omni (six-face) shadow map,
    // so a moon goes dark behind its planet and casts an eclipse shadow on it.
    auto* sun_material = make_material(rendering_engine::util::color{255, 220, 120, 255}, 1.0f);
    sun_material->set_emissive(rendering_engine::util::color{255, 210, 110, 255});
    sun_material->set_emissive_intensity(3.0f);

    // The sun is a childless leaf, so giving it a scale is safe; its point
    // light sits at the same node and scale never touches a translation.
    runtime::node* sun = make_visual(root, math::vec3{0.0f, 0.0f, 0.0f}, 1.0f, sun_material);
    sun->name = "sun";
    auto sun_light = std::make_unique<rendering_engine::point_light>();
    sun_light->color = math::vec3{1.0f, 0.96f, 0.88f};
    sun_light->intensity = 3.0f;
    sun_light->constant_attenuation = 1.0f;
    sun_light->linear_attenuation = 0.0f;
    sun_light->quadratic_attenuation = 0.0f;
    sun_light->cast_shadow = true;
    sun->add_component<runtime::light_component>(runtime::light_component{std::move(sun_light)});

    // Planets: distance, radius, orbit rate (rad/s, all same sign), colour,
    // moon count. Inner planets orbit faster, the classic look.
    make_planet(root, 1.7f, 0.35f, 0.70f, rendering_engine::util::color{120, 170, 255, 255}, 0);
    make_planet(root, 2.7f, 0.50f, 0.50f, rendering_engine::util::color{220, 110, 80, 255}, 1);
    make_planet(root, 3.7f, 0.70f, 0.34f, rendering_engine::util::color{210, 180, 120, 255}, 2);
    make_planet(root, 4.6f, 0.45f, 0.24f, rendering_engine::util::color{150, 220, 200, 255}, 1);
}

static void on_engine_stop(const core::engine_stop& event)
{
    (void)event;
    // Destroying the nodes frees their components, which unregister their models
    // and the sun's light from the renderer. Do this — and drop the materials —
    // before the engine tears the renderer and GPU device down.
    g_spinners.clear();
    g_nodes.clear();
    g_materials.clear();
    g_ambient.reset();
    // Drop the last reference to the shared sphere so its GPU buffers are freed
    // (the models held the others) before the renderer and device tear down.
    g_sphere.reset();
}

static void on_render_update(const core::render_update& event)
{
    g_time += event.m_delta_time / 1000.0f;

    // Drive each orbit/moon pivot from absolute time about the world up axis
    // (+Z), so the system lies flat in the XY plane — the natural orientation
    // for this Z-up engine.
    for (const spinner& s : g_spinners)
    {
        s.node->transform.set_rotation(math::vec3{0.0f, 0.0f, s.rate * g_time});
    }
}

GAME_MODULE()
{
    LOG_INF("Registering external module: scene_graph_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_render_update = on_render_update;
    register_game_module(info);
    return true;
}
