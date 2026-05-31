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

    // Resolution of each procedurally generated cube face. Small enough to
    // build instantly, large enough that the mip chain gives a clean
    // roughness sweep.
    constexpr uint32_t face_size = 128;

    std::unique_ptr<rendering_engine::environment> g_environment;
    std::unique_ptr<rendering_engine::sphere> g_sphere;
    std::unique_ptr<rendering_engine::directional_light> g_sun;

    // The world-space direction a cube texel faces, matching the OpenGL
    // cube-map convention the @ref environment samples with. Must stay in
    // lock-step with environment.cpp's dir_for_face_uv so the generated
    // faces and the sampler agree.
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

    // A simple analytic HDR sky: a horizon-to-zenith gradient about the
    // engine's +Z up axis, a dim ground hemisphere, and a bright sun disk
    // with a soft glow. Returns linear radiance (the sun core exceeds 1).
    math::vec3 sky_radiance(const math::vec3& dir)
    {
        const math::vec3 horizon{0.70f, 0.80f, 0.95f};
        const math::vec3 zenith{0.12f, 0.32f, 0.80f};
        const math::vec3 ground{0.22f, 0.20f, 0.18f};

        math::vec3 color;
        if (dir.z >= 0.0f)
        {
            color = math::lerp(horizon, zenith, std::pow(dir.z, 0.5f));
        }
        else
        {
            color = math::lerp(horizon, ground, std::pow(-dir.z, 0.4f));
        }

        // Sun roughly toward +X so it faces the default camera at -X.
        const math::vec3 sun_dir = math::normalize(math::vec3{1.0f, -0.35f, 0.5f});
        const float s = std::max(math::dot(dir, sun_dir), 0.0f);
        const float disk = std::pow(s, 800.0f) * 30.0f;
        const float glow = std::pow(s, 8.0f) * 1.4f;
        color += math::vec3{1.0f, 0.96f, 0.88f} * (disk + glow);
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
        // scene's background + ambient source. set_environment points the
        // skybox pass at the cube map and attaches the same environment to
        // the built-in standard material.
        g_environment = std::make_unique<rendering_engine::environment>(face_size, generate_sky_faces());
        renderer.set_environment(g_environment.get());

        // A polished metal sphere so the prefiltered specular reflection of
        // the sky reads clearly; a little roughness softens it into the mip
        // chain rather than a perfect mirror.
        auto& material = renderer.get_standard_material();
        material.set_base_color(rendering_engine::util::color{250, 250, 250, 255});
        material.set_metalness(1.0f);
        material.set_roughness(0.2f);

        g_sphere = std::make_unique<rendering_engine::sphere>(&material);
        g_sphere->upload();
        renderer.register_scene_renderable(g_sphere.get());

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

        // Clear the environment before the object dies so neither the
        // skybox pass nor the material keeps a dangling cube-map handle.
        renderer.set_environment(nullptr);
        renderer.unregister_scene_renderable(g_sphere.get());

        g_sun.reset();
        g_sphere.reset();
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
