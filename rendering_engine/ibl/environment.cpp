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
#include <string>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <runtime/engine.hpp>

namespace
{
    namespace math = core::math;
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

    // ---- GPU compute path -------------------------------------------------
    //
    // Three compute shaders convolve the derived tables directly into
    // storage images. dir_for_face mirrors the CPU @ref dir_for_face_uv so
    // the hardware samplerCube and the written cube agree on orientation.

    // Shared GLSL helpers: the cube-face direction mapping, Hammersley
    // sequence and GGX importance sampling. Prepended to each shader.
    const std::string compute_prelude = R"glsl(
        #version 450
        const float PI = 3.14159265359;

        vec3 dir_for_face(int face, vec2 uv)
        {
            float sc = 2.0 * uv.x - 1.0;
            float tc = 2.0 * uv.y - 1.0;
            if (face == 0) return normalize(vec3(1.0, -tc, -sc));
            if (face == 1) return normalize(vec3(-1.0, -tc, sc));
            if (face == 2) return normalize(vec3(sc, 1.0, tc));
            if (face == 3) return normalize(vec3(sc, -1.0, -tc));
            if (face == 4) return normalize(vec3(sc, -tc, 1.0));
            return normalize(vec3(-sc, -tc, -1.0));
        }

        float radical_inverse_vdc(uint bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return float(bits) * 2.3283064365386963e-10;
        }

        vec2 hammersley(uint i, uint n)
        {
            return vec2(float(i) / float(n), radical_inverse_vdc(i));
        }

        // GGX lobe sample around an arbitrary normal N.
        vec3 importance_sample_ggx(vec2 xi, vec3 N, float roughness)
        {
            float a = roughness * roughness;
            float phi = 2.0 * PI * xi.x;
            float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
            float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
            vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
            vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
            vec3 tangent = normalize(cross(up, N));
            vec3 bitangent = cross(N, tangent);
            return normalize(tangent * H.x + bitangent * H.y + N * H.z);
        }
    )glsl";

    // Diffuse irradiance: cosine-weighted hemisphere convolution per output
    // texel, written into an imageCube (z = face).
    const std::string irradiance_body = R"glsl(
        layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
        layout(set = 0, binding = 0) uniform samplerCube envMap;
        layout(rgba16f, set = 0, binding = 1) uniform writeonly imageCube outIrradiance;

        void main()
        {
            ivec2 size = imageSize(outIrradiance);
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            int face = int(gl_GlobalInvocationID.z);
            if (p.x >= size.x || p.y >= size.y)
            {
                return;
            }
            vec2 uv = (vec2(p) + 0.5) / vec2(size);
            vec3 N = dir_for_face(face, uv);

            vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
            vec3 right = normalize(cross(up, N));
            up = cross(N, right);

            vec3 irradiance = vec3(0.0);
            float samples = 0.0;
            const float sampleDelta = 0.025;
            for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
            {
                for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
                {
                    vec3 tangent = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
                    vec3 dir = tangent.x * right + tangent.y * up + tangent.z * N;
                    irradiance += texture(envMap, dir).rgb * cos(theta) * sin(theta);
                    samples += 1.0;
                }
            }
            irradiance = PI * irradiance / max(samples, 1.0);
            imageStore(outIrradiance, ivec3(p, face), vec4(irradiance, 1.0));
        }
    )glsl";

    // Prefiltered specular: GGX importance sampling per output texel, with
    // a mip bias on the source lookup to suppress fireflies. One dispatch
    // per mip level supplies the roughness via the Params UBO.
    const std::string prefilter_body = R"glsl(
        layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
        layout(set = 0, binding = 0) uniform samplerCube envMap;
        layout(rgba16f, set = 0, binding = 1) uniform writeonly imageCube outPrefiltered;
        layout(set = 0, binding = 2, std140) uniform Params
        {
            vec4 data; // x roughness, y source resolution
        } u;

        float distribution_ggx(float nDotH, float roughness)
        {
            float a = roughness * roughness;
            float a2 = a * a;
            float d = (nDotH * nDotH * (a2 - 1.0) + 1.0);
            return a2 / (PI * d * d);
        }

        void main()
        {
            ivec2 size = imageSize(outPrefiltered);
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            int face = int(gl_GlobalInvocationID.z);
            if (p.x >= size.x || p.y >= size.y)
            {
                return;
            }
            float roughness = u.data.x;
            float resolution = u.data.y;
            vec2 uv = (vec2(p) + 0.5) / vec2(size);
            vec3 N = dir_for_face(face, uv);
            vec3 V = N;

            const uint SAMPLE_COUNT = 1024u;
            vec3 prefiltered = vec3(0.0);
            float totalWeight = 0.0;
            for (uint i = 0u; i < SAMPLE_COUNT; ++i)
            {
                vec2 xi = hammersley(i, SAMPLE_COUNT);
                vec3 H = importance_sample_ggx(xi, N, roughness);
                vec3 L = normalize(2.0 * dot(V, H) * H - V);
                float nDotL = max(dot(N, L), 0.0);
                if (nDotL > 0.0)
                {
                    float nDotH = max(dot(N, H), 0.0);
                    float hDotV = max(dot(H, V), 0.0);
                    float d = distribution_ggx(nDotH, roughness);
                    float pdf = (d * nDotH / (4.0 * hDotV)) + 0.0001;
                    float saTexel = 4.0 * PI / (6.0 * resolution * resolution);
                    float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
                    float mip = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
                    prefiltered += textureLod(envMap, L, mip).rgb * nDotL;
                    totalWeight += nDotL;
                }
            }
            prefiltered = prefiltered / max(totalWeight, 0.001);
            imageStore(outPrefiltered, ivec3(p, face), vec4(prefiltered, 1.0));
        }
    )glsl";

    // Split-sum environment BRDF integration into a 2D rg table.
    const std::string brdf_body = R"glsl(
        layout(local_size_x = 8, local_size_y = 8) in;
        layout(rgba16f, set = 0, binding = 0) uniform writeonly image2D outLut;

        float geometry_schlick_ggx(float nDotX, float roughness)
        {
            float a = roughness;
            float k = (a * a) / 2.0;
            return nDotX / (nDotX * (1.0 - k) + k);
        }

        float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
        {
            return geometry_schlick_ggx(max(dot(N, V), 0.0), roughness) *
                   geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);
        }

        vec2 integrate_brdf(float nDotV, float roughness)
        {
            vec3 V = vec3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
            vec3 N = vec3(0.0, 0.0, 1.0);
            float scale = 0.0;
            float bias = 0.0;
            const uint SAMPLE_COUNT = 1024u;
            for (uint i = 0u; i < SAMPLE_COUNT; ++i)
            {
                vec2 xi = hammersley(i, SAMPLE_COUNT);
                vec3 H = importance_sample_ggx(xi, N, roughness);
                vec3 L = normalize(2.0 * dot(V, H) * H - V);
                float nDotL = max(L.z, 0.0);
                float nDotH = max(H.z, 0.0);
                float vDotH = max(dot(V, H), 0.0);
                if (nDotL > 0.0)
                {
                    float g = geometry_smith(N, V, L, roughness);
                    float gVis = (g * vDotH) / (nDotH * nDotV);
                    float fc = pow(1.0 - vDotH, 5.0);
                    scale += (1.0 - fc) * gVis;
                    bias += fc * gVis;
                }
            }
            return vec2(scale, bias) / float(SAMPLE_COUNT);
        }

        void main()
        {
            ivec2 size = imageSize(outLut);
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            if (p.x >= size.x || p.y >= size.y)
            {
                return;
            }
            vec2 uv = (vec2(p) + 0.5) / vec2(size);
            vec2 result = integrate_brdf(uv.x, uv.y);
            imageStore(outLut, p, vec4(result, 0.0, 1.0));
        }
    )glsl";
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
        auto& gpu = *runtime::current_engine().gpu;
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
        if (m_prefiltered.valid())
        {
            gpu.destroy(m_prefiltered);
            m_prefiltered = {};
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

    gpu::texture environment::prefiltered() const
    {
        // The GPU path builds a dedicated GGX-convolved cube; the CPU
        // fallback reuses the source mip chain as the specular source.
        return m_prefiltered.valid() ? m_prefiltered : m_skybox;
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
        upload_source(face_size, faces);

        auto& gpu = *runtime::current_engine().gpu;
        if (gpu.supports_compute_prefilter())
        {
            build_derived_gpu(face_size);
        }
        else
        {
            build_derived_cpu(face_size, faces);
        }
    }

    void environment::upload_source(uint32_t face_size, const std::array<std::vector<float>, 6>& faces)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Source skybox cube: HDR with a full mip chain. Mip 0 is the
        // sharp background sampled by the skybox pass; the GPU prefilter
        // reads coarser mips to suppress fireflies. clamp_edge avoids face
        // seams; trilinear filtering smooths sampling.
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
    }

    void environment::build_derived_cpu(uint32_t face_size, const std::array<std::vector<float>, 6>& faces)
    {
        auto& gpu = *runtime::current_engine().gpu;

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

        LOG_INF("Built IBL environment on CPU (skybox %ux%u, irradiance %u, brdf %u)",
                face_size,
                face_size,
                irradiance_size,
                brdf_size);
    }

    void environment::build_derived_gpu(uint32_t face_size)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Temporaries are tracked so the whole convolution scaffold is
        // released once the tables are written; only the three textures
        // outlive this function.
        std::vector<gpu::shader_module> shaders;
        std::vector<gpu::pipeline> pipelines;
        std::vector<gpu::bind_group_layout> layouts;
        std::vector<gpu::bind_group> groups;
        std::vector<gpu::buffer> buffers;

        const auto make_compute = [&](const std::string& body, const gpu::bind_group_layout_descriptor& layout_desc)
        {
            gpu::shader_module_descriptor sd{};
            sd.stage = gpu::shader_stage::compute;
            sd.spirv = gpu::compile_glsl_to_spirv(compute_prelude + body, gpu::shader_stage::compute);
            const gpu::shader_module shader = gpu.create_shader_module(sd);
            shaders.push_back(shader);

            const gpu::bind_group_layout layout = gpu.create_bind_group_layout(layout_desc);
            layouts.push_back(layout);

            gpu::compute_pipeline_descriptor pd{};
            pd.compute_shader = shader;
            pd.bind_group_layouts.push_back(layout);
            const gpu::pipeline pipe = gpu.create_compute_pipeline(pd);
            pipelines.push_back(pipe);
            return std::pair<gpu::pipeline, gpu::bind_group_layout>{pipe, layout};
        };

        // Storage-capable cube/2D texture descriptor helper.
        const auto storage_texture = [&](gpu::texture_dimension dim, uint32_t size, bool mipmapped)
        {
            gpu::texture_descriptor td{};
            td.dimension = dim;
            td.format = gpu::texture_format::rgba16_float;
            td.width = size;
            td.height = size;
            td.mipmaps = mipmapped;
            td.min_filter = gpu::filter_mode::linear;
            td.mag_filter = gpu::filter_mode::linear;
            td.mipmap_filter = mipmapped ? gpu::mipmap_mode::linear : gpu::mipmap_mode::none;
            td.address_u = gpu::address_mode::clamp_edge;
            td.address_v = gpu::address_mode::clamp_edge;
            td.address_w = gpu::address_mode::clamp_edge;
            return gpu.create_texture(td);
        };

        // Layout entry helpers.
        const auto sampler_entry = [](uint32_t binding)
        { return gpu::bind_group_layout_entry{binding, gpu::binding_kind::texture}; };
        const auto image_entry = [](uint32_t binding)
        {
            gpu::bind_group_layout_entry e{binding, gpu::binding_kind::storage_texture};
            e.storage_format = gpu::texture_format::rgba16_float;
            e.storage_access_mode = gpu::storage_access::write_only;
            return e;
        };
        const auto ubo_entry = [](uint32_t binding)
        { return gpu::bind_group_layout_entry{binding, gpu::binding_kind::uniform_buffer}; };

        // ---- pipelines --------------------------------------------------
        gpu::bind_group_layout_descriptor irr_layout{};
        irr_layout.entries.push_back(sampler_entry(0));
        irr_layout.entries.push_back(image_entry(1));
        const auto [irr_pipe, irr_bgl] = make_compute(irradiance_body, irr_layout);

        gpu::bind_group_layout_descriptor pre_layout{};
        pre_layout.entries.push_back(sampler_entry(0));
        pre_layout.entries.push_back(image_entry(1));
        pre_layout.entries.push_back(ubo_entry(2));
        const auto [pre_pipe, pre_bgl] = make_compute(prefilter_body, pre_layout);

        gpu::bind_group_layout_descriptor brdf_layout{};
        brdf_layout.entries.push_back(image_entry(0));
        const auto [brdf_pipe, brdf_bgl] = make_compute(brdf_body, brdf_layout);

        // ---- output textures -------------------------------------------
        m_irradiance = storage_texture(gpu::texture_dimension::cube, irradiance_size, false);
        m_prefiltered = storage_texture(gpu::texture_dimension::cube, face_size, true);
        m_brdf_lut = storage_texture(gpu::texture_dimension::d2, brdf_size, false);
        // Allocate the prefiltered mip chain so each level can be bound as
        // an image; the compute pass overwrites every level afterwards.
        gpu.generate_mipmaps(m_prefiltered);

        const uint32_t prefilter_mips =
            1u + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(face_size))));

        // ---- bind groups ------------------------------------------------
        const auto sampler_binding = [](uint32_t binding, gpu::texture tex)
        {
            gpu::binding_value v{};
            v.binding = binding;
            v.kind = gpu::binding_kind::texture;
            v.texture_value = tex;
            return v;
        };
        const auto image_binding = [](uint32_t binding, gpu::texture tex, uint32_t level)
        {
            gpu::binding_value v{};
            v.binding = binding;
            v.kind = gpu::binding_kind::storage_texture;
            v.texture_value = tex;
            v.storage_level = level;
            return v;
        };
        const auto ubo_binding = [](uint32_t binding, gpu::buffer buf)
        {
            gpu::binding_value v{};
            v.binding = binding;
            v.kind = gpu::binding_kind::uniform_buffer;
            v.buffer_value = buf;
            return v;
        };

        gpu::bind_group_descriptor irr_bg_desc{};
        irr_bg_desc.layout = irr_bgl;
        irr_bg_desc.entries.push_back(sampler_binding(0, m_skybox));
        irr_bg_desc.entries.push_back(image_binding(1, m_irradiance, 0));
        const gpu::bind_group irr_bg = gpu.create_bind_group(irr_bg_desc);
        groups.push_back(irr_bg);

        gpu::bind_group_descriptor brdf_bg_desc{};
        brdf_bg_desc.layout = brdf_bgl;
        brdf_bg_desc.entries.push_back(image_binding(0, m_brdf_lut, 0));
        const gpu::bind_group brdf_bg = gpu.create_bind_group(brdf_bg_desc);
        groups.push_back(brdf_bg);

        // One bind group + Params UBO per prefilter mip (roughness varies).
        std::vector<gpu::bind_group> prefilter_groups;
        std::vector<uint32_t> prefilter_sizes;
        for (uint32_t mip = 0; mip < prefilter_mips; ++mip)
        {
            const float roughness =
                prefilter_mips > 1 ? static_cast<float>(mip) / static_cast<float>(prefilter_mips - 1) : 0.0f;
            const std::array<float, 4> params{roughness, static_cast<float>(face_size), 0.0f, 0.0f};

            gpu::buffer_descriptor bd{};
            bd.size = sizeof(params);
            bd.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
            bd.hint = gpu::buffer_usage_hint::static_data;
            bd.initial_data = params.data();
            const gpu::buffer ubo = gpu.create_buffer(bd);
            buffers.push_back(ubo);

            gpu::bind_group_descriptor desc{};
            desc.layout = pre_bgl;
            desc.entries.push_back(sampler_binding(0, m_skybox));
            desc.entries.push_back(image_binding(1, m_prefiltered, mip));
            desc.entries.push_back(ubo_binding(2, ubo));
            const gpu::bind_group bg = gpu.create_bind_group(desc);
            groups.push_back(bg);
            prefilter_groups.push_back(bg);
            prefilter_sizes.push_back(std::max(1u, face_size >> mip));
        }

        // ---- dispatch ---------------------------------------------------
        constexpr uint32_t local = 8;
        const auto groups_for = [](uint32_t extent) { return (extent + local - 1) / local; };

        auto encoder = gpu.create_command_encoder();
        {
            auto pass = encoder->begin_compute_pass();

            pass->set_pipeline(brdf_pipe);
            pass->set_bind_group(0, brdf_bg);
            pass->dispatch(groups_for(brdf_size), groups_for(brdf_size), 1);

            pass->set_pipeline(irr_pipe);
            pass->set_bind_group(0, irr_bg);
            pass->dispatch(groups_for(irradiance_size), groups_for(irradiance_size), 6);

            pass->set_pipeline(pre_pipe);
            for (uint32_t mip = 0; mip < prefilter_mips; ++mip)
            {
                const uint32_t size = prefilter_sizes[mip];
                pass->set_bind_group(0, prefilter_groups[mip]);
                pass->dispatch(groups_for(size), groups_for(size), 6);
            }

            pass->end();
        }
        // Make the image stores visible to the samplers that read these
        // textures in later passes.
        encoder->barrier(gpu::pipeline_stage_compute_shader,
                         gpu::pipeline_stage_fragment_shader,
                         gpu::access_storage_image_write,
                         gpu::access_shader_read);
        gpu.submit(std::move(encoder));

        // The scaffold is single-use; release it now that the tables hold
        // their results. The three output textures persist.
        for (auto g : groups)
        {
            gpu.destroy(g);
        }
        for (auto b : buffers)
        {
            gpu.destroy(b);
        }
        for (auto p : pipelines)
        {
            gpu.destroy(p);
        }
        for (auto l : layouts)
        {
            gpu.destroy(l);
        }
        for (auto s : shaders)
        {
            gpu.destroy(s);
        }

        LOG_INF("Built IBL environment on GPU (skybox %ux%u, prefilter mips %u, irradiance %u, brdf %u)",
                face_size,
                face_size,
                prefilter_mips,
                irradiance_size,
                brdf_size);
    }
} // namespace rendering_engine
