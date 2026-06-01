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

#include <cmath>
#include <memory>
#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/materials/points_material.hpp>
#include <rendering_engine/renderables/points.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>

namespace
{
    namespace math = infrastructure::math;

    // Number of points on the Fibonacci-sphere cloud.
    constexpr int point_count = 2000;

    std::unique_ptr<rendering_engine::points> g_points;
    float g_rotation = 0.0f;
    const float g_rotation_speed = 3.14f / 6;
} // namespace

static void on_engine_start(const event_engine::engine_start& event)
{
    auto& material = control::current_engine().renderer->get_points_material();
    material.set_size(6.0f);
    material.set_size_attenuation(true);
    material.set_color(rendering_engine::util::color{255, 255, 255, 255});

    // Scatter points evenly over a unit sphere with the Fibonacci
    // spiral, colouring each by its position so the cloud reads in 3D.
    std::vector<math::vec3> positions;
    std::vector<math::vec3> colors;
    positions.reserve(point_count);
    colors.reserve(point_count);

    const float golden_angle = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < point_count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(point_count - 1);
        const float z = 1.0f - 2.0f * t;
        const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float theta = golden_angle * static_cast<float>(i);
        const math::vec3 position{radius * std::cos(theta), radius * std::sin(theta), z};
        positions.push_back(position);
        colors.push_back(math::vec3{0.5f + 0.5f * position.x, 0.5f + 0.5f * position.y, 0.5f + 0.5f * position.z});
    }

    g_points = std::make_unique<rendering_engine::points>(&material);
    g_points->set_positions(positions, colors);
    g_points->upload();
    control::current_engine().renderer->register_scene_renderable(g_points.get());
}

static void on_engine_stop(const event_engine::engine_stop& event)
{
    control::current_engine().renderer->unregister_scene_renderable(g_points.get());
    g_points.reset();
}

static void on_frame(const event_engine::frame& event)
{
    g_rotation += g_rotation_speed * (static_cast<float>(get_delta_time()) / 1000);
    g_points->transform.set_rotation(math::vec3{0.0f, 0.0f, g_rotation});
}

GAME_MODULE()
{
    LOG_INF("Registering external module: points_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_frame = on_frame;
    register_game_module(info);
    return true;
}
