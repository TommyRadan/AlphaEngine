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

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/materials/points_material.hpp>
#include <rendering_engine/renderables/points.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>

#include <memory>
#include <vector>

static std::unique_ptr<rendering_engine::points_material> g_material;
static std::unique_ptr<rendering_engine::points> g_points;

static void on_engine_start(const event_engine::engine_start& event)
{
    g_material = control::current_engine().renderer->create_points_material();
    g_material->set_color(rendering_engine::util::color{120, 200, 255, 255});
    g_material->set_point_size(6.0f);

    // A small generated point cloud: a cube lattice centred on the
    // origin so the camera (looking at the origin) sees a cluster of
    // points filling the view.
    std::vector<infrastructure::math::vec3> positions;
    const int grid = 6;
    const float spacing = 0.3f;
    const float offset = static_cast<float>(grid - 1) * spacing * 0.5f;
    for (int x = 0; x < grid; ++x)
    {
        for (int y = 0; y < grid; ++y)
        {
            for (int z = 0; z < grid; ++z)
            {
                positions.push_back(infrastructure::math::vec3{static_cast<float>(x) * spacing - offset,
                                                               static_cast<float>(y) * spacing - offset,
                                                               static_cast<float>(z) * spacing - offset});
            }
        }
    }

    g_points = std::make_unique<rendering_engine::points>(g_material.get());
    g_points->set_positions(positions);
    g_points->upload();
    control::current_engine().renderer->register_scene_renderable(g_points.get());
}

static void on_engine_stop(const event_engine::engine_stop& event)
{
    control::current_engine().renderer->unregister_scene_renderable(g_points.get());
    g_points.reset();
    g_material.reset();
}

GAME_MODULE()
{
    LOG_INF("Registering external module: points_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    register_game_module(info);
    return true;
}
