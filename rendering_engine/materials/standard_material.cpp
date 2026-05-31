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

#include <rendering_engine/materials/standard_material.hpp>

#include <array>
#include <string>

#include <control/engine.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/ibl/environment.hpp>
#include <rendering_engine/mesh/tangent.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace
{
    // Binding numbers. UBOs share a single namespace across every
    // descriptor set on the OpenGL backend (ARB_gl_spirv), so they must
    // stay globally unique: the scene_pass owns camera = 0 and lights = 2
    // in the per-frame set, the per-draw model matrix takes 1, leaving 3
    // for the per-material params block. The five samplers live in their
    // own namespace but must still differ from the UBO within the Vulkan
    // per-material set, so they run 4..8.
    constexpr uint32_t draw_model_binding = 1;
    constexpr uint32_t material_params_binding = 3;
    constexpr uint32_t material_albedo_map_binding = 4;
    constexpr uint32_t material_normal_map_binding = 5;
    constexpr uint32_t material_metalness_map_binding = 6;
    constexpr uint32_t material_roughness_map_binding = 7;
    constexpr uint32_t material_emissive_map_binding = 8;

    // Image-based-lighting samplers. The shadow map (9) and shadow UBO
    // (10) are spent by the scene pass's per-frame set, so the IBL set
    // resumes at 11: the irradiance and prefiltered specular cube maps and
    // the 2D BRDF look-up table. They stay unique across both the UBO and
    // sampler namespaces to satisfy the OpenGL ARB_gl_spirv flattening.
    constexpr uint32_t material_irradiance_map_binding = 11;
    constexpr uint32_t material_prefiltered_map_binding = 12;
    constexpr uint32_t material_brdf_lut_binding = 13;

    // std140 layout for the per-material params UBO, six vec4 rows:
    //   0  baseColor   (rgb tint, a alpha)
    //   16 emissive    (rgb colour, a intensity)
    //   32 params      (x metalness, y roughness, z opacity)
    //   48 mapFlags    (x albedo, y normal, z metalness, w roughness)
    //   64 mapFlags2   (x emissive)
    //   80 iblParams   (x ibl enabled, y ibl intensity)
    // 96 bytes total.
    constexpr size_t material_ubo_size = 96;

    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in vec3 normal;
        layout(location = 3) in vec4 tangent;

        layout(location = 0) out vec3 worldPosition;
        layout(location = 1) out vec3 worldNormal;
        layout(location = 2) out vec2 texCoord;
        layout(location = 3) out vec3 cameraPosition;
        layout(location = 4) out vec4 worldTangent;

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
            // The tangent is a direction along the surface, so it rides
            // the model matrix directly; the handedness sign passes
            // through in .w to rebuild the bitangent.
            worldTangent = vec4(mat3(u_draw.modelMatrix) * tangent.xyz, tangent.w);
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
        layout(location = 4) in vec4 worldTangent;

        layout(location = 0) out vec4 fragColor;

        const int MAX_DIRECTIONAL = 4;
        const int MAX_POINT = 16;
        const float PI = 3.14159265359;

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
            vec4 baseColor;
            vec4 emissive; // rgb colour, a intensity
            vec4 params;   // x metalness, y roughness, z opacity
            vec4 mapFlags; // x albedo, y normal, z metalness, w roughness
            vec4 mapFlags2; // x emissive
            vec4 iblParams; // x enabled, y intensity
        } u_material;

        layout(set = 2, binding = 4) uniform sampler2D albedoMap;
        layout(set = 2, binding = 5) uniform sampler2D normalMap;
        layout(set = 2, binding = 6) uniform sampler2D metalnessMap;
        layout(set = 2, binding = 7) uniform sampler2D roughnessMap;
        layout(set = 2, binding = 8) uniform sampler2D emissiveMap;

        // Image-based lighting: the diffuse irradiance cube, the
        // prefiltered specular cube (the skybox mip chain), and the
        // split-sum environment BRDF table. Sampled only when
        // u_material.iblParams.x is set.
        layout(set = 2, binding = 11) uniform samplerCube irradianceMap;
        layout(set = 2, binding = 12) uniform samplerCube prefilteredMap;
        layout(set = 2, binding = 13) uniform sampler2D brdfLut;

        vec3 shading_normal()
        {
            vec3 N = normalize(worldNormal);
            if (u_material.mapFlags.y == 0.0)
            {
                return N;
            }
            // Gram-Schmidt re-orthonormalize the interpolated tangent
            // against the normal, then rebuild the bitangent with the
            // stored handedness.
            vec3 T = normalize(worldTangent.xyz - N * dot(N, worldTangent.xyz));
            vec3 B = cross(N, T) * worldTangent.w;
            vec3 sampled = texture(normalMap, texCoord).xyz * 2.0 - 1.0;
            return normalize(mat3(T, B, N) * sampled);
        }

        float distribution_ggx(vec3 N, vec3 H, float roughness)
        {
            float a = roughness * roughness;
            float a2 = a * a;
            float nDotH = max(dot(N, H), 0.0);
            float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
            return a2 / (PI * denom * denom);
        }

        float geometry_schlick_ggx(float nDotX, float roughness)
        {
            float r = roughness + 1.0;
            float k = (r * r) / 8.0;
            return nDotX / (nDotX * (1.0 - k) + k);
        }

        float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
        {
            float nDotV = max(dot(N, V), 0.0);
            float nDotL = max(dot(N, L), 0.0);
            return geometry_schlick_ggx(nDotV, roughness) * geometry_schlick_ggx(nDotL, roughness);
        }

        vec3 fresnel_schlick(float cosTheta, vec3 F0)
        {
            return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
        }

        // Fresnel with a roughness-aware ceiling so rough surfaces do not
        // over-brighten at grazing angles under image-based lighting.
        vec3 fresnel_schlick_roughness(float cosTheta, vec3 F0, float roughness)
        {
            vec3 Fr = max(vec3(1.0 - roughness), F0);
            return F0 + (Fr - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
        }

        // Image-based ambient: diffuse irradiance plus prefiltered specular
        // weighted by the split-sum environment BRDF. The prefiltered cube
        // is the skybox mip chain, so perceptual roughness selects the LOD.
        vec3 ibl_ambient(vec3 N, vec3 V, vec3 albedo, float metalness, float roughness, vec3 F0)
        {
            float nDotV = max(dot(N, V), 0.0);
            vec3 F = fresnel_schlick_roughness(nDotV, F0, roughness);
            vec3 kD = (1.0 - F) * (1.0 - metalness);

            vec3 irradiance = texture(irradianceMap, N).rgb;
            vec3 diffuse = irradiance * albedo;

            vec3 R = reflect(-V, N);
            float maxLod = float(textureQueryLevels(prefilteredMap) - 1);
            vec3 prefiltered = textureLod(prefilteredMap, R, roughness * maxLod).rgb;
            vec2 envBrdf = texture(brdfLut, vec2(nDotV, roughness)).rg;
            vec3 specular = prefiltered * (F * envBrdf.x + envBrdf.y);

            return (kD * diffuse + specular) * u_material.iblParams.y;
        }

        vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metalness, float roughness, vec3 F0)
        {
            float nDotL = max(dot(N, L), 0.0);
            if (nDotL <= 0.0)
            {
                return vec3(0.0);
            }
            vec3 H = normalize(V + L);
            float NDF = distribution_ggx(N, H, roughness);
            float G = geometry_smith(N, V, L, roughness);
            vec3 F = fresnel_schlick(max(dot(H, V), 0.0), F0);

            vec3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * nDotL + 0.0001;
            vec3 specular = numerator / denominator;

            // Energy left over after the Fresnel reflection becomes
            // diffuse; pure metals have no diffuse term.
            vec3 kD = (vec3(1.0) - F) * (1.0 - metalness);
            return (kD * albedo / PI + specular) * radiance * nDotL;
        }

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
            vec3 albedo = u_material.baseColor.rgb;
            if (u_material.mapFlags.x != 0.0)
            {
                albedo *= texture(albedoMap, texCoord).rgb;
            }
            float metalness = u_material.params.x;
            if (u_material.mapFlags.z != 0.0)
            {
                metalness *= texture(metalnessMap, texCoord).r;
            }
            float roughness = u_material.params.y;
            if (u_material.mapFlags.w != 0.0)
            {
                roughness *= texture(roughnessMap, texCoord).r;
            }
            // Clamp roughness away from zero so the GGX denominator and
            // the specular highlight stay finite.
            roughness = clamp(roughness, 0.04, 1.0);

            vec3 N = shading_normal();
            vec3 V = normalize(cameraPosition - worldPosition);
            vec3 F0 = mix(vec3(0.04), albedo, metalness);

            vec3 Lo = vec3(0.0);

            for (int i = 0; i < u_lights.counts.x; ++i)
            {
                vec3 L = normalize(-u_lights.directional[i].direction.xyz);
                vec3 radiance = u_lights.directional[i].color.rgb;
                float shadow = directional_shadow(i, N, L);
                Lo += shadow * brdf(N, V, L, radiance, albedo, metalness, roughness, F0);
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
                Lo += brdf(N, V, L, radiance, albedo, metalness, roughness, F0);
            }

            vec3 ambient;
            if (u_material.iblParams.x > 0.5)
            {
                ambient = ibl_ambient(N, V, albedo, metalness, roughness, F0);
            }
            else
            {
                // Flat ambient fallback when no environment is attached;
                // pure metals reflect nothing without one, so the ambient
                // diffuse fades out as metalness rises.
                ambient = u_lights.ambient.rgb * albedo * (1.0 - metalness);
            }

            vec3 emissive = u_material.emissive.rgb * u_material.emissive.a;
            if (u_material.mapFlags2.x != 0.0)
            {
                emissive *= texture(emissiveMap, texCoord).rgb;
            }

            vec3 color = ambient + Lo + emissive;
            fragColor = vec4(color, u_material.baseColor.a * u_material.params.z);
        }
)fs";
} // namespace

namespace rendering_engine
{
    standard_material::standard_material(gpu::bind_group_layout frame_layout)
    {
        // Position+uv+normal+tangent stream; the layout helper bakes the
        // attribute offsets that match vertex_position_uv_normal_tangent.
        gpu::vertex_buffer_layout vertex_layout = vertex_position_uv_normal_tangent_layout();
        vertex_layout.stride = 0;

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching every 3D renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({draw_model_binding, gpu::binding_kind::uniform_buffer});

        // Per-material layout (slot 2): the params UBO plus the five PBR
        // samplers, all owned by this material.
        gpu::bind_group_layout_descriptor material_layout{};
        material_layout.entries.push_back({material_params_binding, gpu::binding_kind::uniform_buffer});
        material_layout.entries.push_back({material_albedo_map_binding, gpu::binding_kind::texture});
        material_layout.entries.push_back({material_normal_map_binding, gpu::binding_kind::texture});
        material_layout.entries.push_back({material_metalness_map_binding, gpu::binding_kind::texture});
        material_layout.entries.push_back({material_roughness_map_binding, gpu::binding_kind::texture});
        material_layout.entries.push_back({material_emissive_map_binding, gpu::binding_kind::texture});
        material_layout.entries.push_back({material_irradiance_map_binding, gpu::binding_kind::texture});
        material_layout.entries.push_back({material_prefiltered_map_binding, gpu::binding_kind::texture});
        material_layout.entries.push_back({material_brdf_lut_binding, gpu::binding_kind::texture});

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

    standard_material::~standard_material()
    {
        // Drop the bind group before the buffers / textures it
        // references, then null it so the base destructor's
        // destruct_pipeline does not double-free.
        auto& gpu = *control::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
        release_map(m_emissive_map);
        release_map(m_roughness_map);
        release_map(m_metalness_map);
        release_map(m_normal_map);
        release_map(m_albedo_map);
        if (m_material_ubo.valid())
        {
            gpu.destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    uint32_t standard_material::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    void standard_material::set_base_color(const util::color& color)
    {
        m_base_color = color;
        upload_params();
    }

    void standard_material::set_metalness(float metalness)
    {
        m_metalness = metalness;
        upload_params();
    }

    void standard_material::set_roughness(float roughness)
    {
        m_roughness = roughness;
        upload_params();
    }

    void standard_material::set_emissive(const util::color& color)
    {
        m_emissive = color;
        upload_params();
    }

    void standard_material::set_emissive_intensity(float intensity)
    {
        m_emissive_intensity = intensity;
        upload_params();
    }

    void standard_material::set_albedo_map(const util::image& image)
    {
        release_map(m_albedo_map);
        m_albedo_map = upload_map(image);
        rebuild_bind_group();
    }

    void standard_material::clear_albedo_map()
    {
        if (!m_albedo_map.valid())
        {
            return;
        }
        release_map(m_albedo_map);
        rebuild_bind_group();
    }

    void standard_material::set_normal_map(const util::image& image)
    {
        release_map(m_normal_map);
        m_normal_map = upload_map(image);
        rebuild_bind_group();
    }

    void standard_material::clear_normal_map()
    {
        if (!m_normal_map.valid())
        {
            return;
        }
        release_map(m_normal_map);
        rebuild_bind_group();
    }

    void standard_material::set_metalness_map(const util::image& image)
    {
        release_map(m_metalness_map);
        m_metalness_map = upload_map(image);
        rebuild_bind_group();
    }

    void standard_material::clear_metalness_map()
    {
        if (!m_metalness_map.valid())
        {
            return;
        }
        release_map(m_metalness_map);
        rebuild_bind_group();
    }

    void standard_material::set_roughness_map(const util::image& image)
    {
        release_map(m_roughness_map);
        m_roughness_map = upload_map(image);
        rebuild_bind_group();
    }

    void standard_material::clear_roughness_map()
    {
        if (!m_roughness_map.valid())
        {
            return;
        }
        release_map(m_roughness_map);
        rebuild_bind_group();
    }

    void standard_material::set_emissive_map(const util::image& image)
    {
        release_map(m_emissive_map);
        m_emissive_map = upload_map(image);
        rebuild_bind_group();
    }

    void standard_material::clear_emissive_map()
    {
        if (!m_emissive_map.valid())
        {
            return;
        }
        release_map(m_emissive_map);
        rebuild_bind_group();
    }

    void standard_material::set_environment(const environment& env)
    {
        m_environment = &env;
        rebuild_bind_group();
    }

    void standard_material::clear_environment()
    {
        if (m_environment == nullptr)
        {
            return;
        }
        m_environment = nullptr;
        rebuild_bind_group();
    }

    gpu::texture standard_material::upload_map(const util::image& image)
    {
        auto& gpu = *control::current_engine().gpu;

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
        gpu::texture map = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
        gpu.write_texture(map, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(map);
        return map;
    }

    void standard_material::release_map(gpu::texture& map)
    {
        if (!map.valid())
        {
            return;
        }
        auto& gpu = *control::current_engine().gpu;
        gpu.destroy(map);
        map = {};
    }

    void standard_material::rebuild_bind_group()
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

        const std::array<std::pair<uint32_t, gpu::texture>, 5> maps = {{
            {material_albedo_map_binding, m_albedo_map},
            {material_normal_map_binding, m_normal_map},
            {material_metalness_map_binding, m_metalness_map},
            {material_roughness_map_binding, m_roughness_map},
            {material_emissive_map_binding, m_emissive_map},
        }};
        for (const auto& [binding, texture] : maps)
        {
            gpu::binding_value tex_slot{};
            tex_slot.binding = binding;
            tex_slot.kind = gpu::binding_kind::texture;
            tex_slot.texture_value = texture;
            bg_descriptor.entries.push_back(tex_slot);
        }

        // IBL set: bind the environment's tables, or invalid handles when
        // none is attached. The iblParams flag gates the sampling, so the
        // dormant handles are never read — matching how the scene pass
        // leaves the shadow map invalid until a caster exists.
        const std::array<std::pair<uint32_t, gpu::texture>, 3> ibl_maps = {{
            {material_irradiance_map_binding, m_environment != nullptr ? m_environment->irradiance() : gpu::texture{}},
            {material_prefiltered_map_binding, m_environment != nullptr ? m_environment->skybox() : gpu::texture{}},
            {material_brdf_lut_binding, m_environment != nullptr ? m_environment->brdf_lut() : gpu::texture{}},
        }};
        for (const auto& [binding, texture] : ibl_maps)
        {
            gpu::binding_value tex_slot{};
            tex_slot.binding = binding;
            tex_slot.kind = gpu::binding_kind::texture;
            tex_slot.texture_value = texture;
            bg_descriptor.entries.push_back(tex_slot);
        }

        m_per_material_bind_group = gpu.create_bind_group(bg_descriptor);

        upload_params();
    }

    void standard_material::upload_params()
    {
        std::array<float, 24> payload{};
        payload[0] = static_cast<float>(m_base_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_base_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_base_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_base_color.a) / 255.0f;
        payload[4] = static_cast<float>(m_emissive.r) / 255.0f;
        payload[5] = static_cast<float>(m_emissive.g) / 255.0f;
        payload[6] = static_cast<float>(m_emissive.b) / 255.0f;
        payload[7] = m_emissive_intensity;
        payload[8] = m_metalness;
        payload[9] = m_roughness;
        payload[10] = m_params.opacity;
        payload[12] = m_albedo_map.valid() ? 1.0f : 0.0f;
        payload[13] = m_normal_map.valid() ? 1.0f : 0.0f;
        payload[14] = m_metalness_map.valid() ? 1.0f : 0.0f;
        payload[15] = m_roughness_map.valid() ? 1.0f : 0.0f;
        payload[16] = m_emissive_map.valid() ? 1.0f : 0.0f;
        // iblParams row at offset 80 (float index 20): enable flag + the
        // intensity multiplier the shader applies to the ambient term.
        payload[20] = m_environment != nullptr ? 1.0f : 0.0f;
        payload[21] = 1.0f;

        auto& gpu = *control::current_engine().gpu;
        gpu.write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
