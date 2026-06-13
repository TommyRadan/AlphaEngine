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

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/renderables/premade_3d/plane.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>
#include <runtime/engine.hpp>

#include <memory>
#include <vector>

// A row of spheres marching away from the camera over a long ground
// plane, with linear distance fog: the near spheres read at full colour
// while the far ones dissolve into the haze, showcasing the fog added in
// issue #122.
static std::vector<std::unique_ptr<rendering_engine::sphere>> g_spheres;
static std::unique_ptr<rendering_engine::plane> g_ground;
static std::unique_ptr<rendering_engine::ambient_light> g_ambient;
static std::unique_ptr<rendering_engine::directional_light> g_sun;

// Linear RGB the scene fades into; matched to the fog colour so the
// receding plane and spheres melt into a uniform haze.
static const rendering_engine::util::color g_fog_color{90, 115, 160, 255};

static void on_engine_start(const core::engine_start& event)
{
    auto& renderer = *runtime::current_engine().renderer;

    auto& material = renderer.get_phong_material();
    material.set_diffuse(rendering_engine::util::color{230, 126, 34, 255});
    material.set_specular(rendering_engine::util::color{255, 255, 255, 255});
    material.set_shininess(48.0f);

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
            auto ball = std::make_unique<rendering_engine::sphere>(&material);
            const float x = static_cast<float>(row) * 4.0f;
            const float y = (static_cast<float>(col) - static_cast<float>(lateral_cols - 1) * 0.5f) * 3.0f;
            ball->transform.set_position(core::math::vec3{x, y, 0.0f});
            ball->upload();
            renderer.register_scene_renderable(ball.get());
            g_spheres.push_back(std::move(ball));
        }
    }

    // A long ground plane below the spheres; world up is +Z, so the
    // plane's default +Z normal already faces the sky.
    g_ground = std::make_unique<rendering_engine::plane>(&material, 120.0f, 120.0f);
    g_ground->transform.set_position(core::math::vec3{16.0f, 0.0f, -1.5f});
    g_ground->upload();
    renderer.register_scene_renderable(g_ground.get());

    // Lights self-register on construction; keeping them alive is enough
    // for the scene pass to pack them into the per-frame lights UBO.
    g_ambient = std::make_unique<rendering_engine::ambient_light>();
    g_ambient->color = core::math::vec3{1.0f, 1.0f, 1.0f};
    g_ambient->intensity = 0.2f;

    g_sun = std::make_unique<rendering_engine::directional_light>();
    g_sun->direction = core::math::vec3{1.0f, -0.4f, -0.6f};
    g_sun->color = core::math::vec3{1.0f, 0.97f, 0.9f};
    g_sun->intensity = 1.0f;

    // Linear distance fog: clear up close, fully saturated by the far
    // end of the sphere row.
    rendering_engine::fog_settings fog{};
    fog.mode = rendering_engine::fog_mode::linear;
    fog.color = core::math::vec3{static_cast<float>(g_fog_color.r) / 255.0f,
                                 static_cast<float>(g_fog_color.g) / 255.0f,
                                 static_cast<float>(g_fog_color.b) / 255.0f};
    fog.near_distance = 3.0f;
    fog.far_distance = 18.0f;
    renderer.set_fog(fog);
}

static void on_engine_stop(const core::engine_stop& event)
{
    auto& renderer = *runtime::current_engine().renderer;
    renderer.set_fog(rendering_engine::fog_settings{});
    for (auto& ball : g_spheres)
    {
        renderer.unregister_scene_renderable(ball.get());
    }
    g_spheres.clear();
    renderer.unregister_scene_renderable(g_ground.get());
    g_ground.reset();
    g_sun.reset();
    g_ambient.reset();
}

GAME_MODULE()
{
    LOG_INF("Registering external module: fog_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    register_game_module(info);
    return true;
}
