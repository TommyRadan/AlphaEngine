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

#include <rendering_engine/materials/phong_material.hpp>

#include <array>
#include <string>

#include <control/engine.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace
{
    // Binding numbers. UBOs share a single namespace across every
    // descriptor set on the OpenGL backend (ARB_gl_spirv), so they must
    // stay globally unique: the scene_pass owns camera = 0 and lights = 2
    // in the per-frame set, the per-draw model matrix takes 1, leaving 3
    // for the per-material params block. The sampler lives in its own
    // namespace but must still differ from the UBO within the Vulkan
    // per-material set, so it takes 4.
    constexpr uint32_t draw_model_binding = 1;
    constexpr uint32_t material_params_binding = 3;
    constexpr uint32_t material_diffuse_map_binding = 4;

    // std140 layout for the per-material params UBO: vec4 diffuseColor
    // at offset 0, vec4 specular (rgb colour, a shininess) at offset 16,
    // vec4 misc (x useTexture) at offset 32. The struct is 48 bytes.
    constexpr size_t material_ubo_size = 48;

    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in vec3 normal;

        layout(location = 0) out vec3 worldPosition;
        layout(location = 1) out vec3 worldNormal;
        layout(location = 2) out vec2 texCoord;
        layout(location = 3) out vec3 cameraPosition;

        layout(set = 0, binding = 0, std140) uniform PerFrame
        {
            mat4 viewMatrix;
            mat4 projectionMatrix;
        } u_frame;

        layout(set = 1, binding = 1, std140) uniform PerDraw
        {
            mat4 modelMatrix;
        } u_draw;

        void main()
        {
            vec4 world = u_draw.modelMatrix * vec4(position, 1.0);
            worldPosition = world.xyz;
            // Inverse-transpose of the upper-left 3x3 so non-uniform
            // scale does not skew the shading normal.
            mat3 normalMatrix = transpose(inverse(mat3(u_draw.modelMatrix)));
            worldNormal = normalMatrix * normal;
            texCoord = uv;
            // Camera world position is the translation column of the
            // inverse view matrix; derived here so the shared per-frame
            // UBO need not carry it.
            cameraPosition = inverse(u_frame.viewMatrix)[3].xyz;
            gl_Position = u_frame.projectionMatrix * u_frame.viewMatrix * world;
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) in vec3 worldPosition;
        layout(location = 1) in vec3 worldNormal;
        layout(location = 2) in vec2 texCoord;
        layout(location = 3) in vec3 cameraPosition;

        layout(location = 0) out vec4 fragColor;

        const int MAX_DIRECTIONAL = 4;
        const int MAX_POINT = 16;

        struct DirectionalLight
        {
            vec4 direction; // xyz normalized, w unused
            vec4 color;     // rgb radiance * intensity, a unused
        };

        struct PointLight
        {
            vec4 position;    // xyz world, w unused
            vec4 color;       // rgb radiance * intensity, a unused
            vec4 attenuation; // x range, y constant, z linear, w quadratic
        };

        layout(set = 0, binding = 2, std140) uniform Lights
        {
            vec4 ambient;
            ivec4 counts; // x directional, y point
            DirectionalLight directional[MAX_DIRECTIONAL];
            PointLight point[MAX_POINT];
        } u_lights;

        // Directional shadow data, owned by the scene pass alongside the
        // lights block. params: x enabled, y bias, z caster light index.
        layout(set = 0, binding = 10, std140) uniform Shadow
        {
            mat4 lightViewProj;
            vec4 params;
        } u_shadow;

        layout(set = 0, binding = 9) uniform sampler2D shadowMap;

        layout(set = 2, binding = 3, std140) uniform Material
        {
            vec4 diffuseColor;
            vec4 specular; // rgb specular colour, a shininess
            vec4 misc;     // x useTexture
        } u_material;

        layout(set = 2, binding = 4) uniform sampler2D diffuseMap;

        // 1.0 = fully lit, 0.0 = fully shadowed. Only the caster light
        // index is occluded; every other directional light returns 1.0.
        // 3x3 PCF softens the shadow edge by one texel.
        float directional_shadow(int lightIndex, vec3 N, vec3 L)
        {
            if (u_shadow.params.x == 0.0 || lightIndex != int(u_shadow.params.z))
            {
                return 1.0;
            }
            vec4 lightClip = u_shadow.lightViewProj * vec4(worldPosition, 1.0);
            vec3 proj = lightClip.xyz / lightClip.w;
            proj = proj * 0.5 + 0.5;
            if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
            {
                return 1.0;
            }
            float bias = max(u_shadow.params.y * (1.0 - dot(N, L)), u_shadow.params.y * 0.1);
            vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
            float lit = 0.0;
            for (int x = -1; x <= 1; ++x)
            {
                for (int y = -1; y <= 1; ++y)
                {
                    float closest = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
                    lit += (proj.z - bias > closest) ? 0.0 : 1.0;
                }
            }
            return lit / 9.0;
        }

        void main()
        {
            vec3 N = normalize(worldNormal);
            vec3 V = normalize(cameraPosition - worldPosition);

            vec3 diffuseAlbedo = u_material.diffuseColor.rgb;
            if (u_material.misc.x != 0.0)
            {
                diffuseAlbedo *= texture(diffuseMap, texCoord).rgb;
            }
            vec3 specularColor = u_material.specular.rgb;
            float shininess = max(u_material.specular.a, 1.0);

            vec3 result = u_lights.ambient.rgb * diffuseAlbedo;

            for (int i = 0; i < u_lights.counts.x; ++i)
            {
                vec3 L = normalize(-u_lights.directional[i].direction.xyz);
                float nDotL = max(dot(N, L), 0.0);
                vec3 radiance = u_lights.directional[i].color.rgb;
                float shadow = directional_shadow(i, N, L);
                result += shadow * nDotL * radiance * diffuseAlbedo;
                if (nDotL > 0.0)
                {
                    vec3 H = normalize(L + V);
                    float nDotH = max(dot(N, H), 0.0);
                    result += shadow * pow(nDotH, shininess) * radiance * specularColor;
                }
            }

            for (int i = 0; i < u_lights.counts.y; ++i)
            {
                vec3 toLight = u_lights.point[i].position.xyz - worldPosition;
                float dist = length(toLight);
                float range = u_lights.point[i].attenuation.x;
                if (range > 0.0 && dist > range)
                {
                    continue;
                }
                vec3 L = toLight / max(dist, 0.0001);
                float constant = u_lights.point[i].attenuation.y;
                float linear = u_lights.point[i].attenuation.z;
                float quadratic = u_lights.point[i].attenuation.w;
                float atten = 1.0 / (constant + linear * dist + quadratic * dist * dist);
                vec3 radiance = u_lights.point[i].color.rgb * atten;
                float nDotL = max(dot(N, L), 0.0);
                result += nDotL * radiance * diffuseAlbedo;
                if (nDotL > 0.0)
                {
                    vec3 H = normalize(L + V);
                    float nDotH = max(dot(N, H), 0.0);
                    result += pow(nDotH, shininess) * radiance * specularColor;
                }
            }

            fragColor = vec4(result, u_material.diffuseColor.a);
        }
)fs";
} // namespace

namespace rendering_engine
{
    phong_material::phong_material(gpu::bind_group_layout frame_layout)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — per-renderable strides are supplied at
        // set_vertex_buffer time. Position sits at offset 0, UV at
        // offset 12 and normal at offset 20 across the
        // position+uv+normal vertex stream this pipeline draws, so the
        // baked attribute offsets stay valid.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 2, gpu::scalar_type::float32, sizeof(float) * 3});
        vertex_layout.attributes.push_back({2, 3, gpu::scalar_type::float32, sizeof(float) * 5});

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching every 3D renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({draw_model_binding, gpu::binding_kind::uniform_buffer});

        // Per-material layout (slot 2): the params UBO plus the diffuse
        // sampler, both owned by this material.
        gpu::bind_group_layout_descriptor material_layout{};
        material_layout.entries.push_back({material_params_binding, gpu::binding_kind::uniform_buffer});
        material_layout.entries.push_back({material_diffuse_map_binding, gpu::binding_kind::texture});

        // Opaque lit surface: depth tested and written, no blending.
        material_params params{};
        params.transparent = false;
        params.depth_test = true;
        params.depth_write = true;

        construct_pipeline(
            vertex_shader, fragment_shader, vertex_layout, draw_layout, frame_layout, params, material_layout);

        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        auto& gpu = *control::current_engine().gpu;
        m_material_ubo = gpu.create_buffer(ubo_descriptor);

        rebuild_bind_group();
    }

    phong_material::~phong_material()
    {
        // Drop the bind group before the buffers / texture it
        // references, then null it so the base destructor's
        // destruct_pipeline does not double-free.
        auto& gpu = *control::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
        if (m_diffuse_map.valid())
        {
            gpu.destroy(m_diffuse_map);
            m_diffuse_map = {};
        }
        if (m_material_ubo.valid())
        {
            gpu.destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    uint32_t phong_material::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    void phong_material::set_diffuse(const util::color& color)
    {
        m_diffuse = color;
        upload_params();
    }

    void phong_material::set_specular(const util::color& color)
    {
        m_specular = color;
        upload_params();
    }

    void phong_material::set_shininess(float shininess)
    {
        m_shininess = shininess;
        upload_params();
    }

    void phong_material::set_diffuse_map(const util::image& image)
    {
        auto& gpu = *control::current_engine().gpu;
        if (m_diffuse_map.valid())
        {
            gpu.destroy(m_diffuse_map);
            m_diffuse_map = {};
        }

        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = gpu::texture_format::rgba8_unorm;
        descriptor.width = image.get_width();
        descriptor.height = image.get_height();
        descriptor.mipmaps = true;
        descriptor.min_filter = gpu::filter_mode::linear;
        descriptor.mag_filter = gpu::filter_mode::linear;
        descriptor.address_u = gpu::address_mode::repeat;
        descriptor.address_v = gpu::address_mode::repeat;
        descriptor.address_w = gpu::address_mode::repeat;
        m_diffuse_map = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
        gpu.write_texture(m_diffuse_map, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(m_diffuse_map);

        rebuild_bind_group();
    }

    void phong_material::clear_diffuse_map()
    {
        if (!m_diffuse_map.valid())
        {
            return;
        }
        auto& gpu = *control::current_engine().gpu;
        gpu.destroy(m_diffuse_map);
        m_diffuse_map = {};
        rebuild_bind_group();
    }

    void phong_material::rebuild_bind_group()
    {
        auto& gpu = *control::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }

        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = m_per_material_layout;

        gpu::binding_value ubo_slot{};
        ubo_slot.binding = material_params_binding;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_material_ubo;
        bg_descriptor.entries.push_back(ubo_slot);

        gpu::binding_value tex_slot{};
        tex_slot.binding = material_diffuse_map_binding;
        tex_slot.kind = gpu::binding_kind::texture;
        tex_slot.texture_value = m_diffuse_map;
        bg_descriptor.entries.push_back(tex_slot);

        m_per_material_bind_group = gpu.create_bind_group(bg_descriptor);

        upload_params();
    }

    void phong_material::upload_params()
    {
        std::array<float, 12> payload{};
        payload[0] = static_cast<float>(m_diffuse.r) / 255.0f;
        payload[1] = static_cast<float>(m_diffuse.g) / 255.0f;
        payload[2] = static_cast<float>(m_diffuse.b) / 255.0f;
        payload[3] = static_cast<float>(m_diffuse.a) / 255.0f;
        payload[4] = static_cast<float>(m_specular.r) / 255.0f;
        payload[5] = static_cast<float>(m_specular.g) / 255.0f;
        payload[6] = static_cast<float>(m_specular.b) / 255.0f;
        payload[7] = m_shininess;
        payload[8] = m_diffuse_map.valid() ? 1.0f : 0.0f;

        auto& gpu = *control::current_engine().gpu;
        gpu.write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
