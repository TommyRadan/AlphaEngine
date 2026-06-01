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
 * Lighting: the sun sphere is emissive so it reads as the source, with a dim
 * point light at the origin for radial fill. Cast shadows come from a
 * directional key light (this engine shadow-maps directional lights only, not
 * point lights, over a fixed ±6 frustum at the origin), so moons throw eclipse
 * shadows onto their planets and the whole system is sized to fit that box.
 */

#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/mesh/mesh.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>
#include <scene_graph/light_component.hpp>
#include <scene_graph/mesh_component.hpp>
#include <scene_graph/node.hpp>
#include <scene_graph/scene_graph.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace math = infrastructure::math;

// A node that turns about the world X axis at a fixed rate; the orbit pivots
// (planets one way, moons the other) are all just spinners.
struct spinner
{
    scene_graph::node* node;
    float rate; // radians per second; sign sets the direction
};

// Everything is owned here — the scene-graph root only holds non-owning links,
// and materials must not outlive the renderer.
static std::vector<std::unique_ptr<scene_graph::node>> g_nodes;
static std::vector<std::unique_ptr<rendering_engine::standard_material>> g_materials;
static std::vector<spinner> g_spinners;
static std::unique_ptr<rendering_engine::ambient_light> g_ambient;
static std::unique_ptr<rendering_engine::directional_light> g_sun_dir;
static rendering_engine::mesh g_sphere;
static float g_time = 0.0f;

static rendering_engine::mesh make_sphere(int stacks, int slices)
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

    rendering_engine::mesh mesh;
    mesh.upload_obj(v);
    return mesh;
}

static scene_graph::node* make_child(scene_graph::node& parent)
{
    g_nodes.push_back(std::make_unique<scene_graph::node>());
    scene_graph::node* node = g_nodes.back().get();
    parent.add(*node);
    return node;
}

static rendering_engine::standard_material* make_material(const rendering_engine::util::color& base, float roughness)
{
    auto material = control::current_engine().renderer->create_standard_material();
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
static scene_graph::node* make_visual(scene_graph::node& parent,
                                      const math::vec3& offset,
                                      float radius,
                                      rendering_engine::standard_material* material)
{
    scene_graph::node* visual = make_child(parent);
    visual->transform.set_position(offset);
    visual->transform.set_scale(math::vec3{radius, radius, radius});
    visual->add_component<scene_graph::mesh_component>(scene_graph::mesh_component{material, g_sphere});
    return visual;
}

// Adds a planet: an orbit pivot (spinner about X) under @p parent, an unscaled
// anchor out at @p distance along +Y, the planet sphere on the anchor, and
// @p moons moons on their own retrograde pivots hung off the same anchor (so
// the planet's scale never reaches them).
static void make_planet(scene_graph::node& parent,
                        float distance,
                        float radius,
                        float orbit_rate,
                        const rendering_engine::util::color& color,
                        int moons)
{
    scene_graph::node* orbit = make_child(parent);
    g_spinners.push_back(spinner{orbit, orbit_rate});

    scene_graph::node* anchor = make_child(*orbit);
    anchor->transform.set_position(math::vec3{distance, 0.0f, 0.0f});

    make_visual(*anchor, math::vec3{0.0f, 0.0f, 0.0f}, radius, make_material(color, 0.8f));

    auto* moon_material = make_material(rendering_engine::util::color{170, 170, 180, 255}, 0.9f);
    for (int i = 0; i < moons; ++i)
    {
        scene_graph::node* moon_pivot = make_child(*anchor);
        // Retrograde: moons sweep opposite to the planets' orbits, and each
        // moon of a planet runs at its own rate so they do not overlap.
        g_spinners.push_back(spinner{moon_pivot, -1.7f - 0.6f * static_cast<float>(i)});

        const float moon_distance = radius + 0.45f + 0.35f * static_cast<float>(i);
        make_visual(*moon_pivot, math::vec3{moon_distance, 0.0f, 0.0f}, 0.15f, moon_material);
    }
}

static void on_engine_start(const event_engine::engine_start& event)
{
    (void)event;

    g_sphere = make_sphere(18, 36);

    // Dim fill so the night side of each body is not pure black.
    g_ambient = std::make_unique<rendering_engine::ambient_light>();
    g_ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    g_ambient->intensity = 0.05f;

    scene_graph::node& root = control::current_engine().scenes->root;

    // The sun: an emissive sphere at the origin so it reads as the source, plus
    // a dim point light at the same spot for a little radial fill (constant
    // attenuation, no distance falloff, so the outer planets still catch it).
    auto* sun_material = make_material(rendering_engine::util::color{255, 220, 120, 255}, 1.0f);
    sun_material->set_emissive(rendering_engine::util::color{255, 210, 110, 255});
    sun_material->set_emissive_intensity(3.0f);

    // The sun is a childless leaf, so giving it a scale is safe; its point
    // light sits at the same node and scale never touches a translation.
    scene_graph::node* sun = make_visual(root, math::vec3{0.0f, 0.0f, 0.0f}, 1.0f, sun_material);
    auto sun_light = std::make_unique<rendering_engine::point_light>();
    sun_light->color = math::vec3{1.0f, 0.96f, 0.88f};
    sun_light->intensity = 0.8f;
    sun_light->constant_attenuation = 1.0f;
    sun_light->linear_attenuation = 0.0f;
    sun_light->quadratic_attenuation = 0.0f;
    sun->add_component<scene_graph::light_component>(scene_graph::light_component{std::move(sun_light)});

    // Shadows: this engine only casts from directional lights (point lights do
    // not), with a fixed orthographic shadow frustum of half-extent 6 centred at
    // the origin (see shadow_pass.cpp / issue #146). So the key light is a
    // shadow-casting directional light aimed roughly across the orbital plane —
    // moons then cast eclipse shadows on their planets — and the whole system is
    // kept inside that ±6 box. A point-light "sun" cannot cast radial shadows
    // here; this is the closest the renderer supports.
    g_sun_dir = std::make_unique<rendering_engine::directional_light>();
    g_sun_dir->direction = math::vec3{1.0f, 0.25f, -0.15f};
    g_sun_dir->color = math::vec3{1.0f, 0.95f, 0.85f};
    g_sun_dir->intensity = 2.2f;
    g_sun_dir->cast_shadow = true;

    // Planets: distance, radius, orbit rate (rad/s, all same sign), colour,
    // moon count. Inner planets orbit faster, the classic look. Distances stay
    // within the ±6 shadow frustum so every body is shadow-mapped.
    make_planet(root, 1.7f, 0.35f, 0.70f, rendering_engine::util::color{120, 170, 255, 255}, 0);
    make_planet(root, 2.7f, 0.50f, 0.50f, rendering_engine::util::color{220, 110, 80, 255}, 1);
    make_planet(root, 3.7f, 0.70f, 0.34f, rendering_engine::util::color{210, 180, 120, 255}, 2);
    make_planet(root, 4.6f, 0.45f, 0.24f, rendering_engine::util::color{150, 220, 200, 255}, 1);
}

static void on_engine_stop(const event_engine::engine_stop& event)
{
    (void)event;
    // Destroying the nodes frees their components, which unregister their models
    // and the sun's light from the renderer. Do this — and drop the materials —
    // before the engine tears the renderer and GPU device down.
    g_spinners.clear();
    g_nodes.clear();
    g_materials.clear();
    g_ambient.reset();
    g_sun_dir.reset();
}

static void on_frame(const event_engine::frame& event)
{
    (void)event;

    g_time += static_cast<float>(get_delta_time()) / 1000.0f;

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
    info.on_frame = on_frame;
    register_game_module(info);
    return true;
}
