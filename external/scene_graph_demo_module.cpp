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
 * @brief Showcase for the entity/component scene graph.
 *
 * Builds a small node hierarchy and animates only its parents, letting the
 * scene graph propagate the motion:
 *
 *   root
 *   └── pivot                 (spins about Z every frame)
 *       ├── orbiter x4        (offset out from the pivot; each also spins)
 *       │     └── moon        (child of the first orbiter — a second level)
 *       └── lamp              (a point light via light_component)
 *
 * Each orbiter carries a mesh_component (a cube), so spinning the pivot makes
 * them revolve around the centre; spinning an orbiter makes its moon revolve
 * around it — pure hierarchical world-matrix propagation, no per-object math in
 * here. The lamp is a light_component whose point light tracks its node, so the
 * highlight sweeps across the cubes as the pivot turns. The camera is provided
 * by camera_module (WASD + mouse to fly around).
 */

#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/materials/phong_material.hpp>
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

// Nodes are owned here (the scene-graph root only holds non-owning links).
static std::vector<std::unique_ptr<scene_graph::node>> g_nodes;
static scene_graph::node* g_pivot = nullptr;
static std::vector<scene_graph::node*> g_orbiters;
static std::unique_ptr<rendering_engine::ambient_light> g_ambient;
static rendering_engine::mesh g_cube;

static float g_revolve = 0.0f;
static const float g_revolve_speed = 0.6f;
static const int g_orbiter_count = 4;
static const float g_orbit_radius = 2.5f;

static void push_quad(std::vector<rendering_engine::vertex_position_uv_normal>& out,
                      const math::vec3& a,
                      const math::vec3& b,
                      const math::vec3& c,
                      const math::vec3& d,
                      const math::vec3& normal)
{
    const math::vec2 uv00{0.0f, 0.0f};
    const math::vec2 uv10{1.0f, 0.0f};
    const math::vec2 uv11{1.0f, 1.0f};
    const math::vec2 uv01{0.0f, 1.0f};
    out.push_back({a, uv00, normal});
    out.push_back({b, uv10, normal});
    out.push_back({c, uv11, normal});
    out.push_back({a, uv00, normal});
    out.push_back({c, uv11, normal});
    out.push_back({d, uv01, normal});
}

static rendering_engine::mesh make_cube(float h)
{
    std::vector<rendering_engine::vertex_position_uv_normal> v;
    v.reserve(36);
    // +X, -X, +Y, -Y, +Z, -Z faces, wound CCW when viewed from outside.
    push_quad(v, {h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}, {1, 0, 0});
    push_quad(v, {-h, h, -h}, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-1, 0, 0});
    push_quad(v, {h, h, -h}, {-h, h, -h}, {-h, h, h}, {h, h, h}, {0, 1, 0});
    push_quad(v, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0});
    push_quad(v, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
    push_quad(v, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1});

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

static void on_engine_start(const event_engine::engine_start& event)
{
    (void)event;

    auto& material = control::current_engine().renderer->get_phong_material();
    material.set_diffuse(rendering_engine::util::color{230, 126, 34, 255});
    material.set_specular(rendering_engine::util::color{255, 255, 255, 255});
    material.set_shininess(48.0f);

    g_cube = make_cube(0.5f);

    // Soft fill so faces turned away from the lamp are not pure black.
    g_ambient = std::make_unique<rendering_engine::ambient_light>();
    g_ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    g_ambient->intensity = 0.2f;

    scene_graph::node& root = control::current_engine().scenes->root;

    g_pivot = make_child(root);

    for (int i = 0; i < g_orbiter_count; ++i)
    {
        const float angle = (static_cast<float>(i) / g_orbiter_count) * 2.0f * 3.14159265f;
        scene_graph::node* orbiter = make_child(*g_pivot);
        orbiter->transform.set_position(
            math::vec3{g_orbit_radius * std::cos(angle), g_orbit_radius * std::sin(angle), 0.0f});
        orbiter->add_component<scene_graph::mesh_component>(scene_graph::mesh_component{&material, g_cube});
        g_orbiters.push_back(orbiter);
    }

    // A second hierarchy level: a small "moon" cube parented to the first
    // orbiter, so it revolves around that orbiter as the orbiter spins.
    scene_graph::node* moon = make_child(*g_orbiters.front());
    moon->transform.set_position(math::vec3{1.1f, 0.0f, 0.0f});
    moon->transform.set_scale(math::vec3{0.4f, 0.4f, 0.4f});
    moon->add_component<scene_graph::mesh_component>(scene_graph::mesh_component{&material, g_cube});

    // A point light parented to the pivot, so it sweeps around the ring; the
    // light_component drives its position from the node's world transform.
    scene_graph::node* lamp = make_child(*g_pivot);
    lamp->transform.set_position(math::vec3{0.0f, 0.0f, 2.5f});
    auto light = std::make_unique<rendering_engine::point_light>();
    light->color = math::vec3{1.0f, 0.95f, 0.85f};
    light->intensity = 40.0f;
    lamp->add_component<scene_graph::light_component>(scene_graph::light_component{std::move(light)});
}

static void on_engine_stop(const event_engine::engine_stop& event)
{
    (void)event;
    // Destroying the nodes frees their components, which unregister their
    // models and lights from the renderer. Clear before the engine tears the
    // renderer and GPU device down.
    g_orbiters.clear();
    g_pivot = nullptr;
    g_nodes.clear();
    g_ambient.reset();
}

static void on_frame(const event_engine::frame& event)
{
    (void)event;

    const float dt = static_cast<float>(get_delta_time()) / 1000.0f;
    g_revolve += g_revolve_speed * dt;

    if (g_pivot != nullptr)
    {
        // Spin the pivot: every orbiter (and the lamp) revolves with it.
        g_pivot->transform.set_rotation(math::vec3{0.0f, 0.0f, g_revolve});
    }

    // Spin each orbiter on its own axis (twice as fast): the moon parented to
    // the first orbiter revolves around it.
    for (scene_graph::node* orbiter : g_orbiters)
    {
        orbiter->transform.set_rotation(math::vec3{0.0f, 0.0f, g_revolve * 2.0f});
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
