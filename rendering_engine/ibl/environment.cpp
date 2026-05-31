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

#include <rendering_engine/ibl/environment.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace
{
    namespace math = infrastructure::math;
    namespace gpu = rendering_engine::gpu;

    constexpr float pi = 3.14159265358979323846f;

    // Resolution of the cosine-convolved diffuse cube. Irradiance varies
    // slowly across direction, so a small cube captures it without visible
    // banding once bilinearly filtered.
    constexpr uint32_t irradiance_size = 16;

    // Resolution and sample count of the split-sum BRDF table. The table
    // is smooth in both axes, so a modest grid with importance sampling
    // converges cleanly.
    constexpr uint32_t brdf_size = 64;
    constexpr uint32_t brdf_samples = 1024;

    // Hemisphere step counts for the diffuse convolution. The product is
    // the per-output-texel sample budget.
    constexpr uint32_t irradiance_phi_steps = 48;
    constexpr uint32_t irradiance_theta_steps = 12;

    // The direction a cube-map texel faces, given a face index in
    // @c gpu::cube_face order and face-local UV in [0, 1]. The inverse of
    // @ref select_face, using the OpenGL cube-map major-axis convention so
    // the CPU convolution and the GPU sampler agree on orientation.
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

    struct face_sample
    {
        int face;
        float u;
        float v;
    };

    // The cube face and face-local UV a direction maps to — the OpenGL
    // cube-map selection rule, matching @ref dir_for_face_uv.
    face_sample select_face(const math::vec3& dir)
    {
        const float ax = std::fabs(dir.x);
        const float ay = std::fabs(dir.y);
        const float az = std::fabs(dir.z);

        int face = 0;
        float sc = 0.0f;
        float tc = 0.0f;
        float ma = 1.0f;

        if (ax >= ay && ax >= az)
        {
            ma = ax;
            if (dir.x > 0.0f)
            {
                face = 0;
                sc = -dir.z;
                tc = -dir.y;
            }
            else
            {
                face = 1;
                sc = dir.z;
                tc = -dir.y;
            }
        }
        else if (ay >= ax && ay >= az)
        {
            ma = ay;
            if (dir.y > 0.0f)
            {
                face = 2;
                sc = dir.x;
                tc = dir.z;
            }
            else
            {
                face = 3;
                sc = dir.x;
                tc = -dir.z;
            }
        }
        else
        {
            ma = az;
            if (dir.z > 0.0f)
            {
                face = 4;
                sc = dir.x;
                tc = -dir.y;
            }
            else
            {
                face = 5;
                sc = -dir.x;
                tc = -dir.y;
            }
        }

        face_sample result{};
        result.face = face;
        result.u = 0.5f * (sc / ma + 1.0f);
        result.v = 0.5f * (tc / ma + 1.0f);
        return result;
    }

    // Nearest-texel lookup into the in-memory float cube faces. The
    // convolutions average many samples, so point sampling is sufficient
    // and keeps the inner loop branch-free.
    math::vec3 sample_cube(const std::array<std::vector<float>, 6>& faces, uint32_t size, const math::vec3& dir)
    {
        const face_sample fs = select_face(dir);
        const auto clamp_index = [size](float coord)
        {
            int i = static_cast<int>(coord * static_cast<float>(size));
            return static_cast<uint32_t>(std::clamp(i, 0, static_cast<int>(size) - 1));
        };
        const uint32_t x = clamp_index(fs.u);
        const uint32_t y = clamp_index(fs.v);
        const float* texel = &faces[static_cast<size_t>(fs.face)][(static_cast<size_t>(y) * size + x) * 4];
        return math::vec3{texel[0], texel[1], texel[2]};
    }

    // Van der Corput radical inverse — the second Hammersley coordinate.
    float radical_inverse(uint32_t bits)
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return static_cast<float>(bits) * 2.3283064365386963e-10f; // / 2^32
    }

    math::vec2 hammersley(uint32_t i, uint32_t n)
    {
        return math::vec2{static_cast<float>(i) / static_cast<float>(n), radical_inverse(i)};
    }

    // Sample the GGX lobe around +Z (tangent space) for a given roughness.
    math::vec3 importance_sample_ggx(const math::vec2& xi, float roughness)
    {
        const float a = roughness * roughness;
        const float phi = 2.0f * pi * xi.x;
        const float cos_theta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
        const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
        return math::vec3{std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta};
    }

    float geometry_schlick_ggx(float n_dot_x, float roughness)
    {
        // IBL geometry term uses k = a^2 / 2 (Karis), distinct from the
        // direct-lighting (r + 1)^2 / 8 mapping.
        const float a = roughness;
        const float k = (a * a) / 2.0f;
        return n_dot_x / (n_dot_x * (1.0f - k) + k);
    }

    float geometry_smith(float n_dot_v, float n_dot_l, float roughness)
    {
        return geometry_schlick_ggx(n_dot_v, roughness) * geometry_schlick_ggx(n_dot_l, roughness);
    }

    // The split-sum environment BRDF scale/bias at one (N·V, roughness).
    math::vec2 integrate_brdf(float n_dot_v, float roughness)
    {
        const math::vec3 v{std::sqrt(std::max(0.0f, 1.0f - n_dot_v * n_dot_v)), 0.0f, n_dot_v};
        float scale = 0.0f;
        float bias = 0.0f;
        for (uint32_t i = 0; i < brdf_samples; ++i)
        {
            const math::vec2 xi = hammersley(i, brdf_samples);
            const math::vec3 h = importance_sample_ggx(xi, roughness); // around +Z
            const float v_dot_h = std::max(0.0f, math::dot(v, h));
            const math::vec3 l = h * (2.0f * v_dot_h) - v; // reflect(-V, H)
            const float n_dot_l = std::max(0.0f, l.z);
            const float n_dot_h = std::max(0.0f, h.z);
            if (n_dot_l > 0.0f)
            {
                const float g = geometry_smith(n_dot_v, n_dot_l, roughness);
                const float g_vis = (g * v_dot_h) / (n_dot_h * n_dot_v);
                const float fc = std::pow(1.0f - v_dot_h, 5.0f);
                scale += (1.0f - fc) * g_vis;
                bias += fc * g_vis;
            }
        }
        return math::vec2{scale / static_cast<float>(brdf_samples), bias / static_cast<float>(brdf_samples)};
    }

    // sRGB -> linear for the 8-bit image constructor path.
    float srgb_to_linear(uint8_t channel)
    {
        const float c = static_cast<float>(channel) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
} // namespace

namespace rendering_engine
{
    environment::environment(uint32_t face_size, const std::array<std::vector<float>, 6>& faces)
    {
        build(face_size, faces);
    }

    environment::environment(const std::array<util::image, 6>& faces)
    {
        const uint32_t size = faces[0].get_width();
        std::array<std::vector<float>, 6> linear_faces{};
        for (size_t f = 0; f < 6; ++f)
        {
            const util::image& image = faces[f];
            std::vector<float>& dst = linear_faces[f];
            dst.resize(static_cast<size_t>(size) * size * 4);
            for (uint32_t y = 0; y < size; ++y)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const util::color texel = image.get_pixel(x, y);
                    float* out = &dst[(static_cast<size_t>(y) * size + x) * 4];
                    out[0] = srgb_to_linear(texel.r);
                    out[1] = srgb_to_linear(texel.g);
                    out[2] = srgb_to_linear(texel.b);
                    out[3] = 1.0f;
                }
            }
        }
        build(size, linear_faces);
    }

    environment::~environment()
    {
        auto& gpu = *control::current_engine().gpu;
        if (m_brdf_lut.valid())
        {
            gpu.destroy(m_brdf_lut);
            m_brdf_lut = {};
        }
        if (m_irradiance.valid())
        {
            gpu.destroy(m_irradiance);
            m_irradiance = {};
        }
        if (m_skybox.valid())
        {
            gpu.destroy(m_skybox);
            m_skybox = {};
        }
    }

    gpu::texture environment::skybox() const
    {
        return m_skybox;
    }

    gpu::texture environment::irradiance() const
    {
        return m_irradiance;
    }

    gpu::texture environment::brdf_lut() const
    {
        return m_brdf_lut;
    }

    void environment::build(uint32_t face_size, const std::array<std::vector<float>, 6>& faces)
    {
        auto& gpu = *control::current_engine().gpu;

        // Source skybox cube: HDR with a full mip chain. Mip 0 is the
        // sharp background; coarser mips double as the prefiltered specular
        // lobe selected by perceptual roughness. clamp_edge avoids face
        // seams; trilinear filtering smooths the roughness sweep.
        gpu::texture_descriptor skybox_descriptor{};
        skybox_descriptor.dimension = gpu::texture_dimension::cube;
        skybox_descriptor.format = gpu::texture_format::rgba16_float;
        skybox_descriptor.width = face_size;
        skybox_descriptor.height = face_size;
        skybox_descriptor.mipmaps = true;
        skybox_descriptor.min_filter = gpu::filter_mode::linear;
        skybox_descriptor.mag_filter = gpu::filter_mode::linear;
        skybox_descriptor.mipmap_filter = gpu::mipmap_mode::linear;
        skybox_descriptor.address_u = gpu::address_mode::clamp_edge;
        skybox_descriptor.address_v = gpu::address_mode::clamp_edge;
        skybox_descriptor.address_w = gpu::address_mode::clamp_edge;
        m_skybox = gpu.create_texture(skybox_descriptor);

        for (int face = 0; face < 6; ++face)
        {
            const std::vector<float>& data = faces[static_cast<size_t>(face)];
            gpu.write_cube_face(m_skybox, static_cast<gpu::cube_face>(face), data.data(), data.size() * sizeof(float));
        }
        gpu.generate_mipmaps(m_skybox);

        // Diffuse irradiance cube: each output texel integrates the source
        // over a cosine-weighted hemisphere about its direction.
        gpu::texture_descriptor irradiance_descriptor{};
        irradiance_descriptor.dimension = gpu::texture_dimension::cube;
        irradiance_descriptor.format = gpu::texture_format::rgba16_float;
        irradiance_descriptor.width = irradiance_size;
        irradiance_descriptor.height = irradiance_size;
        irradiance_descriptor.mipmaps = false;
        irradiance_descriptor.min_filter = gpu::filter_mode::linear;
        irradiance_descriptor.mag_filter = gpu::filter_mode::linear;
        irradiance_descriptor.mipmap_filter = gpu::mipmap_mode::none;
        irradiance_descriptor.address_u = gpu::address_mode::clamp_edge;
        irradiance_descriptor.address_v = gpu::address_mode::clamp_edge;
        irradiance_descriptor.address_w = gpu::address_mode::clamp_edge;
        m_irradiance = gpu.create_texture(irradiance_descriptor);

        const float d_phi = (2.0f * pi) / static_cast<float>(irradiance_phi_steps);
        const float d_theta = (0.5f * pi) / static_cast<float>(irradiance_theta_steps);
        std::vector<float> irradiance_face(static_cast<size_t>(irradiance_size) * irradiance_size * 4);
        for (int face = 0; face < 6; ++face)
        {
            for (uint32_t y = 0; y < irradiance_size; ++y)
            {
                for (uint32_t x = 0; x < irradiance_size; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(irradiance_size);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(irradiance_size);
                    const math::vec3 normal = dir_for_face_uv(face, u, v);

                    // Build a tangent frame around the surface normal so the
                    // hemisphere samples can be rotated into world space.
                    math::vec3 up =
                        std::fabs(normal.z) < 0.999f ? math::vec3{0.0f, 0.0f, 1.0f} : math::vec3{1.0f, 0.0f, 0.0f};
                    const math::vec3 right = math::normalize(math::cross(up, normal));
                    up = math::cross(normal, right);

                    math::vec3 sum{0.0f, 0.0f, 0.0f};
                    float weight = 0.0f;
                    for (uint32_t p = 0; p < irradiance_phi_steps; ++p)
                    {
                        const float phi = static_cast<float>(p) * d_phi;
                        for (uint32_t t = 0; t < irradiance_theta_steps; ++t)
                        {
                            const float theta = (static_cast<float>(t) + 0.5f) * d_theta;
                            const float sin_theta = std::sin(theta);
                            const float cos_theta = std::cos(theta);
                            const math::vec3 tangent{sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
                            const math::vec3 sample_dir = right * tangent.x + up * tangent.y + normal * tangent.z;
                            sum += sample_cube(faces, face_size, sample_dir) * (cos_theta * sin_theta);
                            weight += 1.0f;
                        }
                    }
                    const math::vec3 irradiance = sum * (pi / std::max(weight, 1.0f));

                    float* out = &irradiance_face[(static_cast<size_t>(y) * irradiance_size + x) * 4];
                    out[0] = irradiance.x;
                    out[1] = irradiance.y;
                    out[2] = irradiance.z;
                    out[3] = 1.0f;
                }
            }
            gpu.write_cube_face(m_irradiance,
                                static_cast<gpu::cube_face>(face),
                                irradiance_face.data(),
                                irradiance_face.size() * sizeof(float));
        }

        // Environment BRDF look-up table: independent of the environment,
        // so it could be cached, but it is cheap enough to rebuild here.
        gpu::texture_descriptor brdf_descriptor{};
        brdf_descriptor.dimension = gpu::texture_dimension::d2;
        brdf_descriptor.format = gpu::texture_format::rgba16_float;
        brdf_descriptor.width = brdf_size;
        brdf_descriptor.height = brdf_size;
        brdf_descriptor.mipmaps = false;
        brdf_descriptor.min_filter = gpu::filter_mode::linear;
        brdf_descriptor.mag_filter = gpu::filter_mode::linear;
        brdf_descriptor.mipmap_filter = gpu::mipmap_mode::none;
        brdf_descriptor.address_u = gpu::address_mode::clamp_edge;
        brdf_descriptor.address_v = gpu::address_mode::clamp_edge;
        brdf_descriptor.address_w = gpu::address_mode::clamp_edge;
        m_brdf_lut = gpu.create_texture(brdf_descriptor);

        std::vector<float> brdf_data(static_cast<size_t>(brdf_size) * brdf_size * 4);
        for (uint32_t y = 0; y < brdf_size; ++y)
        {
            // Roughness on the vertical axis, N·V on the horizontal — the
            // (u, v) the shader samples with (max(N·V, 0), roughness).
            const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(brdf_size);
            for (uint32_t x = 0; x < brdf_size; ++x)
            {
                const float n_dot_v = (static_cast<float>(x) + 0.5f) / static_cast<float>(brdf_size);
                const math::vec2 scale_bias = integrate_brdf(n_dot_v, roughness);
                float* out = &brdf_data[(static_cast<size_t>(y) * brdf_size + x) * 4];
                out[0] = scale_bias.x;
                out[1] = scale_bias.y;
                out[2] = 0.0f;
                out[3] = 1.0f;
            }
        }
        gpu.write_texture(m_brdf_lut, brdf_data.data(), brdf_data.size() * sizeof(float));

        LOG_INF("Built IBL environment (skybox %ux%u, irradiance %u, brdf %u)",
                face_size,
                face_size,
                irradiance_size,
                brdf_size);
    }
} // namespace rendering_engine
