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

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/ibl/environment.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>

namespace
{
    namespace math = infrastructure::math;

    // Resolution of each procedurally generated cube face. 256 keeps the
    // sun crisp and limits aliasing while the GPU prefilter stays cheap.
    constexpr uint32_t face_size = 256;

    // The IBL test grid: roughness sweeps left-to-right; the top row is
    // pure metal, the bottom row a coloured dielectric, so the same
    // environment shows up as both sharpening specular reflections and a
    // soft diffuse tint.
    constexpr int grid_columns = 6;
    constexpr int grid_rows = 2;
    constexpr float grid_spacing = 1.15f;
    constexpr float grid_row_height = 0.95f;
    constexpr float sphere_scale = 0.45f;

    std::unique_ptr<rendering_engine::environment> g_environment;
    std::vector<std::unique_ptr<rendering_engine::standard_material>> g_materials;
    std::vector<std::unique_ptr<rendering_engine::sphere>> g_spheres;
    std::unique_ptr<rendering_engine::directional_light> g_sun;

    // The world-space direction a cube texel faces, matching the OpenGL
    // cube-map convention the @ref environment samples with (kept in
    // lock-step with environment.cpp's dir_for_face_uv).
    math::vec3 dir_for_face_uv(int face, float u, float v)
    {
        const float sc = 2.0f * u - 1.0f;
        const float tc = 2.0f * v - 1.0f;
        switch (face)
        {
        case 0:
            return math::normalize(math::vec3{1.0f, -tc, -sc}); // +X
        case 1:
            return math::normalize(math::vec3{-1.0f, -tc, sc}); // -X
        case 2:
            return math::normalize(math::vec3{sc, 1.0f, tc}); // +Y
        case 3:
            return math::normalize(math::vec3{sc, -1.0f, -tc}); // -Y
        case 4:
            return math::normalize(math::vec3{sc, -tc, 1.0f}); // +Z
        default:
            return math::normalize(math::vec3{-sc, -tc, -1.0f}); // -Z
        }
    }

    // A clear-sky model about the engine's +Z up axis: a saturated
    // horizon-to-zenith gradient, a dim ground hemisphere, and a tight
    // bright sun. Returns linear radiance (the sun core exceeds 1) and is
    // kept dim enough at the horizon that the background does not wash out.
    math::vec3 sky_radiance(const math::vec3& dir)
    {
        const math::vec3 horizon{0.50f, 0.62f, 0.78f};
        const math::vec3 zenith{0.07f, 0.20f, 0.58f};
        const math::vec3 ground{0.17f, 0.15f, 0.13f};

        math::vec3 color;
        if (dir.z >= 0.0f)
        {
            color = math::lerp(horizon, zenith, std::pow(dir.z, 0.45f));
        }
        else
        {
            color = math::lerp(horizon, ground, std::pow(-dir.z, 0.35f));
        }

        // Sun toward +X so it faces the default camera at -X.
        const math::vec3 sun_dir = math::normalize(math::vec3{1.0f, -0.35f, 0.5f});
        const float s = std::max(math::dot(dir, sun_dir), 0.0f);
        const float disk = std::pow(s, 1200.0f) * 18.0f;
        const float glow = std::pow(s, 24.0f) * 0.5f;
        color += math::vec3{1.0f, 0.95f, 0.85f} * (disk + glow);
        return color;
    }

    std::array<std::vector<float>, 6> generate_sky_faces()
    {
        std::array<std::vector<float>, 6> faces{};
        for (int face = 0; face < 6; ++face)
        {
            std::vector<float>& data = faces[static_cast<size_t>(face)];
            data.resize(static_cast<size_t>(face_size) * face_size * 4);
            for (uint32_t y = 0; y < face_size; ++y)
            {
                for (uint32_t x = 0; x < face_size; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(face_size);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(face_size);
                    const math::vec3 color = sky_radiance(dir_for_face_uv(face, u, v));
                    float* texel = &data[(static_cast<size_t>(y) * face_size + x) * 4];
                    texel[0] = color.x;
                    texel[1] = color.y;
                    texel[2] = color.z;
                    texel[3] = 1.0f;
                }
            }
        }
        return faces;
    }

    void on_engine_start(const event_engine::engine_start& /*event*/)
    {
        auto& renderer = *control::current_engine().renderer;

        // Build the IBL environment from the procedural sky and make it the
        // scene background + ambient source. set_environment also stores it
        // so create_standard_material below inherits the lighting.
        g_environment = std::make_unique<rendering_engine::environment>(face_size, generate_sky_faces());
        renderer.set_environment(g_environment.get());

        // A roughness x metalness grid. The camera sits at -X looking
        // toward the origin with +Z up, so the grid is laid out across Y
        // (columns) and Z (rows) at the origin plane.
        for (int row = 0; row < grid_rows; ++row)
        {
            const bool metal = row == 0;
            for (int col = 0; col < grid_columns; ++col)
            {
                auto material = renderer.create_standard_material();
                material->set_metalness(metal ? 1.0f : 0.0f);
                // Crisp mirror at the left, fully rough at the right.
                const float roughness = 0.05f + 0.95f * static_cast<float>(col) / static_cast<float>(grid_columns - 1);
                material->set_roughness(roughness);
                material->set_base_color(metal ? rendering_engine::util::color{245, 245, 245, 255}
                                               : rendering_engine::util::color{220, 70, 50, 255});

                auto ball = std::make_unique<rendering_engine::sphere>(material.get());
                const float y = (static_cast<float>(col) - static_cast<float>(grid_columns - 1) * 0.5f) * grid_spacing;
                const float z = (static_cast<float>(grid_rows - 1) * 0.5f - static_cast<float>(row)) * grid_row_height;
                ball->transform.set_position(math::vec3{0.0f, y, z});
                ball->transform.set_scale(math::vec3{sphere_scale, sphere_scale, sphere_scale});
                ball->upload();
                renderer.register_scene_renderable(ball.get());

                g_materials.push_back(std::move(material));
                g_spheres.push_back(std::move(ball));
            }
        }

        // A warm key light aligned with the sun in the sky so the direct
        // and image-based lighting agree.
        g_sun = std::make_unique<rendering_engine::directional_light>();
        g_sun->direction = math::vec3{-1.0f, 0.35f, -0.5f};
        g_sun->color = math::vec3{1.0f, 0.96f, 0.88f};
        g_sun->intensity = 2.0f;
    }

    void on_engine_stop(const event_engine::engine_stop& /*event*/)
    {
        auto& renderer = *control::current_engine().renderer;

        // Clear the environment before it dies so neither the skybox pass
        // nor any material keeps a dangling cube-map handle.
        renderer.set_environment(nullptr);
        for (auto& ball : g_spheres)
        {
            renderer.unregister_scene_renderable(ball.get());
        }

        g_sun.reset();
        g_spheres.clear();
        g_materials.clear();
        g_environment.reset();
    }
} // namespace

GAME_MODULE()
{
    LOG_INF("Registering external module: skybox_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    register_game_module(info);
    return true;
}
