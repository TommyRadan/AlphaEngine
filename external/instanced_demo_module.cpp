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

#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/materials/instanced_material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/instanced_mesh.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>
#include <runtime/engine.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
    // A grid_side^3 lattice of small cubes drawn from one shared geometry
    // in a single instanced draw call — thousands of objects, one draw.
    // The camera (camera_module) starts at -X looking toward the origin, so
    // the lattice is pushed forward along +X to sit in front of it.
    constexpr uint32_t grid_side = 16;
    constexpr uint32_t instance_count = grid_side * grid_side * grid_side; // 4096
    constexpr float spacing = 0.6f;
    constexpr float cube_scale = 0.3f;
    constexpr float forward_offset = 8.0f;

    std::unique_ptr<rendering_engine::instanced_mesh> g_cubes;
    std::vector<core::math::vec3> g_base_positions;
    float g_time = 0.0f;

    // Builds a unit cube (six single-quad faces, CCW-from-outside winding)
    // in the position+uv+normal vertex format the instanced material draws.
    void build_unit_cube(std::vector<rendering_engine::vertex_position_uv_normal>& vertices,
                         std::vector<uint32_t>& indices)
    {
        using core::math::vec2;
        using core::math::vec3;

        const auto build_face = [&](const vec3& u_axis, const vec3& v_axis, const vec3& w_dir)
        {
            const auto base = static_cast<uint32_t>(vertices.size());
            for (int iy = 0; iy <= 1; ++iy)
            {
                for (int ix = 0; ix <= 1; ++ix)
                {
                    rendering_engine::vertex_position_uv_normal vertex;
                    vertex.pos = u_axis * (static_cast<float>(ix) - 0.5f) + v_axis * (static_cast<float>(iy) - 0.5f) +
                                 w_dir * 0.5f;
                    vertex.normal = w_dir;
                    vertex.uv = vec2{static_cast<float>(ix), static_cast<float>(iy)};
                    vertices.push_back(vertex);
                }
            }
            const uint32_t a = base;
            const uint32_t b = base + 1;
            const uint32_t c = base + 2;
            const uint32_t d = base + 3;
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(b);
        };

        const vec3 ax{1.0f, 0.0f, 0.0f};
        const vec3 ay{0.0f, 1.0f, 0.0f};
        const vec3 az{0.0f, 0.0f, 1.0f};
        build_face(ax, ay, az);   // +Z
        build_face(-az, ay, ax);  // +X
        build_face(-ax, ay, -az); // -Z
        build_face(az, ay, -ax);  // -X
        build_face(ax, -az, ay);  // +Y
        build_face(ax, az, -ay);  // -Y
    }

    void on_engine_start(const core::engine_start& event)
    {
        (void)event;

        auto& material = runtime::current_engine().renderer->get_instanced_material();
        material.set_color(rendering_engine::util::color{255, 255, 255, 255});

        // Fetch the cube geometry through the asset cache so the upload is
        // shared and deduplicated by key rather than baked into this renderable.
        auto cube = runtime::current_engine().assets->get_or_create_mesh(
            "instanced_demo:unit_cube",
            []
            {
                std::vector<rendering_engine::vertex_position_uv_normal> vertices;
                std::vector<uint32_t> indices;
                build_unit_cube(vertices, indices);
                return rendering_engine::mesh_data::from_vertices(vertices, std::move(indices));
            });

        g_cubes = std::make_unique<rendering_engine::instanced_mesh>(&material, instance_count);
        g_cubes->set_geometry(std::move(cube));

        g_base_positions.reserve(instance_count);
        const float centre = static_cast<float>(grid_side - 1) * 0.5f;
        for (uint32_t i = 0; i < grid_side; ++i)
        {
            for (uint32_t j = 0; j < grid_side; ++j)
            {
                for (uint32_t k = 0; k < grid_side; ++k)
                {
                    const uint32_t index = (i * grid_side + j) * grid_side + k;
                    const float x = (static_cast<float>(i) - centre) * spacing + forward_offset;
                    const float y = (static_cast<float>(j) - centre) * spacing;
                    const float z = (static_cast<float>(k) - centre) * spacing;
                    g_base_positions.push_back(core::math::vec3{x, y, z});

                    // Tint each cube by its lattice cell for an RGB gradient.
                    const auto r = static_cast<uint8_t>(40 + (215 * i) / (grid_side - 1));
                    const auto g = static_cast<uint8_t>(40 + (215 * j) / (grid_side - 1));
                    const auto b = static_cast<uint8_t>(40 + (215 * k) / (grid_side - 1));
                    g_cubes->set_instance_color(index, rendering_engine::util::color{r, g, b, 255});
                }
            }
        }

        runtime::current_engine().renderer->register_scene_renderable(g_cubes.get());
        LOG_INF("instanced_demo_module: %u cubes in one instanced draw", instance_count);
    }

    void on_engine_stop(const core::engine_stop& event)
    {
        (void)event;
        runtime::current_engine().renderer->unregister_scene_renderable(g_cubes.get());
        g_cubes.reset();
        g_base_positions.clear();
    }

    void on_frame(const core::frame& event)
    {
        (void)event;
        if (!g_cubes)
        {
            return;
        }

        g_time += event.m_delta_time / 1000.0f;

        const core::math::vec3 spin_axis{0.0f, 1.0f, 0.0f};
        const core::math::vec3 unit_scale{cube_scale, cube_scale, cube_scale};
        for (uint32_t index = 0; index < instance_count; ++index)
        {
            const core::math::vec3& base = g_base_positions[index];
            // A travelling sine wave across the lattice plus a per-cube spin,
            // recomputed every frame for all instances to exercise the
            // dynamic per-instance storage-buffer upload path at scale.
            const float phase = base.x + base.y + base.z;
            const float bob = std::sin(g_time * 2.0f + phase) * 0.25f;
            const core::math::vec3 position{base.x, base.y + bob, base.z};
            const float angle = g_time + phase;

            const core::math::mat4 model =
                core::math::translate(position) * core::math::rotate(angle, spin_axis) * core::math::scale(unit_scale);
            g_cubes->set_instance_transform(index, model);
        }
    }
} // namespace

GAME_MODULE()
{
    LOG_INF("Registering external module: instanced_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_frame = on_frame;
    register_game_module(info);
    return true;
}
