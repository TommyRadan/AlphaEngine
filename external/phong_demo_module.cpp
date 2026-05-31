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

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/renderables/premade_3d/plane.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>

#include <memory>

static std::unique_ptr<rendering_engine::sphere> g_sphere;
static std::unique_ptr<rendering_engine::plane> g_ground;
static std::unique_ptr<rendering_engine::ambient_light> g_ambient;
static std::unique_ptr<rendering_engine::directional_light> g_sun;
static std::unique_ptr<rendering_engine::point_light> g_lamp;

static float g_rotation = 0.0f;
static const float g_rotation_speed = 3.14f / 4;

static void on_engine_start(const event_engine::engine_start& event)
{
    auto& material = control::current_engine().renderer->get_phong_material();
    material.set_diffuse(rendering_engine::util::color{230, 126, 34, 255});
    material.set_specular(rendering_engine::util::color{255, 255, 255, 255});
    material.set_shininess(48.0f);

    g_sphere = std::make_unique<rendering_engine::sphere>(&material);
    g_sphere->upload();
    control::current_engine().renderer->register_scene_renderable(g_sphere.get());

    // A large ground plane below the sphere to catch its shadow. World
    // up is +Z here, so the plane's default +Z normal already faces the
    // sky; drop it just under the unit sphere and scale it out.
    g_ground = std::make_unique<rendering_engine::plane>(&material, 30.0f, 30.0f);
    g_ground->transform.set_position(infrastructure::math::vec3{0.0f, 0.0f, -1.5f});
    g_ground->upload();
    control::current_engine().renderer->register_scene_renderable(g_ground.get());

    // Lights self-register on construction; keeping them alive is enough
    // for the scene pass to pack them into the per-frame lights UBO.
    g_ambient = std::make_unique<rendering_engine::ambient_light>();
    g_ambient->color = infrastructure::math::vec3{1.0f, 1.0f, 1.0f};
    g_ambient->intensity = 0.15f;

    // Camera looks from -X toward the origin, so a sun travelling +X
    // (and slightly down) lights the camera-facing hemisphere. It casts
    // the scene's single shadow map onto the ground plane.
    g_sun = std::make_unique<rendering_engine::directional_light>();
    g_sun->direction = infrastructure::math::vec3{1.0f, -0.4f, -0.6f};
    g_sun->color = infrastructure::math::vec3{1.0f, 0.97f, 0.9f};
    g_sun->intensity = 1.0f;
    g_sun->cast_shadow = true;

    // A cool point light off to the camera side for a coloured highlight
    // that falls off with distance.
    g_lamp = std::make_unique<rendering_engine::point_light>();
    g_lamp->position = infrastructure::math::vec3{-3.0f, 2.0f, 1.5f};
    g_lamp->color = infrastructure::math::vec3{0.4f, 0.6f, 1.0f};
    g_lamp->intensity = 25.0f;
}

static void on_engine_stop(const event_engine::engine_stop& event)
{
    control::current_engine().renderer->unregister_scene_renderable(g_sphere.get());
    control::current_engine().renderer->unregister_scene_renderable(g_ground.get());
    g_sphere.reset();
    g_ground.reset();
    g_lamp.reset();
    g_sun.reset();
    g_ambient.reset();
}

static void on_frame(const event_engine::frame& event)
{
    g_rotation += g_rotation_speed * (static_cast<float>(get_delta_time()) / 1000);
    g_sphere->transform.set_rotation(infrastructure::math::vec3{0.f, 0.f, g_rotation});
}

GAME_MODULE()
{
    LOG_INF("Registering external module: phong_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_frame = on_frame;
    register_game_module(info);
    return true;
}
