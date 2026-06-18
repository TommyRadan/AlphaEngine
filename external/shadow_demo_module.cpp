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
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/renderables/premade_3d/box.hpp>
#include <rendering_engine/renderables/premade_3d/plane.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>
#include <runtime/engine.hpp>

#include <cmath>
#include <memory>
#include <vector>

// Shadow showcase for the auto-fit directional shadow frustum (issue
// #146). A field of spheres and tall pillars sits on a wide ground plane,
// spread past the old fixed light box (+/-6 around the origin) but kept
// within the shadow distance so the whole field stays crisp. With the
// auto-fit:
//
//   * every object casts a sharp shadow wherever it sits on the plane (the
//     old fixed box only reached ~6 units from the origin, so objects past
//     that got no shadow at all);
//   * the pillars cast long thin shadows whose edges stay smooth instead
//     of stair-stepping, because the 4096 map's texels now land on the
//     visible region rather than being spread over empty world space;
//   * the sun slowly orbits, so the shadows sweep across the plane — watch
//     that the edges stay stable (no crawling) thanks to the texel-snapped,
//     bounding-sphere fit.
//
// This is a single cascade, so the crispness holds out to the shadow
// distance (shadow_pass.cpp) and then stops; covering a large world
// sharply is the cascaded-shadow-map follow-on.
//
// To A/B test against the old behaviour, temporarily force the fixed-box
// fallback in rendering_engine/passes/shadow_pass.cpp (skip the camera
// branch in record()) and rebuild: the far objects lose their shadows.
//
// Controls (from camera_module): WASD to move, hold left mouse to look,
// space/ctrl to rise/sink, shift to move faster. Roam around — the
// shadows stay sharp wherever you look.

namespace
{
    namespace math = core::math;

    std::vector<std::unique_ptr<rendering_engine::sphere>> g_spheres;
    std::vector<std::unique_ptr<rendering_engine::box>> g_pillars;
    std::vector<std::unique_ptr<rendering_engine::standard_material>> g_materials;
    std::unique_ptr<rendering_engine::plane> g_ground;
    std::unique_ptr<rendering_engine::standard_material> g_ground_material;
    std::unique_ptr<rendering_engine::ambient_light> g_ambient;
    std::unique_ptr<rendering_engine::directional_light> g_sun;

    // Top of the ground plane in world Z (world up is +Z). Objects rest on it.
    constexpr float ground_z = -1.5f;

    // The sun slowly orbits in azimuth so the shadows sweep across the plane;
    // the downward tilt is held constant so they never grow unbounded.
    float g_sun_angle = 0.0f;
    constexpr float sun_orbit_speed = 0.25f; // radians / second
    constexpr float sun_tilt = -0.65f;       // downward (-Z) component

    rendering_engine::standard_material* make_material(const rendering_engine::util::color& base, float roughness)
    {
        auto material = runtime::current_engine().renderer->create_standard_material();
        material->set_base_color(base);
        material->set_metalness(0.0f);
        material->set_roughness(roughness);
        g_materials.push_back(std::move(material));
        return g_materials.back().get();
    }

    void update_sun_direction()
    {
        g_sun->direction = math::vec3{std::cos(g_sun_angle) * 0.7f, std::sin(g_sun_angle) * 0.7f, sun_tilt};
    }
} // namespace

static void on_engine_start(const core::engine_start& event)
{
    auto& renderer = *runtime::current_engine().renderer;

    // A wide, neutral ground plane to catch the shadows. World up is +Z,
    // so the plane's default +Z normal already faces the sky.
    g_ground_material = renderer.create_standard_material();
    g_ground_material->set_base_color(rendering_engine::util::color{185, 188, 195, 255});
    g_ground_material->set_metalness(0.0f);
    g_ground_material->set_roughness(0.95f);

    g_ground = std::make_unique<rendering_engine::plane>(g_ground_material.get(), 60.0f, 60.0f);
    g_ground->transform.set_position(math::vec3{6.0f, 0.0f, ground_z});
    g_ground->upload();
    renderer.register_scene_renderable(g_ground.get());

    // A few coloured surfaces shared across the field.
    rendering_engine::standard_material* warm = make_material(rendering_engine::util::color{230, 126, 34, 255}, 0.55f);
    rendering_engine::standard_material* cool = make_material(rendering_engine::util::color{52, 152, 219, 255}, 0.4f);
    rendering_engine::standard_material* pale = make_material(rendering_engine::util::color{236, 240, 241, 255}, 0.7f);
    rendering_engine::standard_material* pillar_material =
        make_material(rendering_engine::util::color{120, 200, 140, 255}, 0.6f);

    // A grid of unit spheres spread across the plane. The camera looks
    // from -X, so the grid recedes along +X and spreads across +/-Y. It is
    // kept inside the shadow distance so the whole field stays crisp, yet
    // it reaches well past the old fixed light box (+/-6 around the origin).
    constexpr int depth_rows = 3;   // along +X, away from the camera
    constexpr int lateral_cols = 5; // across +/-Y
    rendering_engine::standard_material* tints[] = {warm, cool, pale};
    for (int row = 0; row < depth_rows; ++row)
    {
        for (int col = 0; col < lateral_cols; ++col)
        {
            rendering_engine::standard_material* tint = tints[(row + col) % 3];
            auto ball = std::make_unique<rendering_engine::sphere>(tint);
            const float x = static_cast<float>(row) * 5.0f;
            const float y = (static_cast<float>(col) - static_cast<float>(lateral_cols - 1) * 0.5f) * 4.0f;
            // Unit sphere (radius 1): centre one radius above the plane so it
            // rests on the ground and casts a contact shadow.
            ball->transform.set_position(math::vec3{x, y, ground_z + 1.0f});
            ball->upload();
            renderer.register_scene_renderable(ball.get());
            g_spheres.push_back(std::move(ball));
        }
    }

    // Tall, thin pillars cast long shadows across the plane — the case
    // where stair-stepping is most obvious, so the smooth edges are the
    // clearest evidence the auto-fit is working.
    const math::vec3 pillar_spots[] = {
        math::vec3{2.0f, -8.0f, 0.0f},
        math::vec3{8.0f, 7.0f, 0.0f},
        math::vec3{11.0f, -3.0f, 0.0f},
    };
    constexpr float pillar_height = 5.0f;
    for (const math::vec3& spot : pillar_spots)
    {
        auto pillar = std::make_unique<rendering_engine::box>(pillar_material, 0.8f, 0.8f, pillar_height);
        pillar->transform.set_position(math::vec3{spot.x, spot.y, ground_z + pillar_height * 0.5f});
        pillar->upload();
        renderer.register_scene_renderable(pillar.get());
        g_pillars.push_back(std::move(pillar));
    }

    // Dim ambient so the shadowed areas read as genuinely dark.
    g_ambient = std::make_unique<rendering_engine::ambient_light>();
    g_ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    g_ambient->intensity = 0.12f;

    // The single shadow-casting directional light. Its frustum is
    // auto-fitted to the camera view every frame (see shadow_pass).
    g_sun = std::make_unique<rendering_engine::directional_light>();
    g_sun->color = math::vec3{1.0f, 0.97f, 0.9f};
    g_sun->intensity = 1.0f;
    g_sun->cast_shadow = true;
    update_sun_direction();
}

static void on_engine_stop(const core::engine_stop& event)
{
    auto& renderer = *runtime::current_engine().renderer;
    for (auto& ball : g_spheres)
    {
        renderer.unregister_scene_renderable(ball.get());
    }
    g_spheres.clear();
    for (auto& pillar : g_pillars)
    {
        renderer.unregister_scene_renderable(pillar.get());
    }
    g_pillars.clear();
    renderer.unregister_scene_renderable(g_ground.get());
    g_ground.reset();
    g_sun.reset();
    g_ambient.reset();
    g_materials.clear();
    g_ground_material.reset();
}

static void on_frame(const core::frame& event)
{
    g_sun_angle += sun_orbit_speed * (event.m_delta_time / 1000.0f);
    update_sun_direction();
}

GAME_MODULE()
{
    LOG_INF("Registering external module: shadow_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_frame = on_frame;
    register_game_module(info);
    return true;
}
