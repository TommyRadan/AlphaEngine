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

#include <rendering_engine/materials/points_material.hpp>

#include <array>
#include <string>

#include <control/engine.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace
{
    // Binding numbers. UBOs share a single namespace across every
    // descriptor set on the OpenGL backend (ARB_gl_spirv), so they must
    // stay globally unique: the scene_pass owns camera = 0 and lights = 2
    // in the per-frame set, the per-draw model matrix takes 1, leaving 3
    // for the per-material params block. The sampler lives in its own
    // namespace but must still differ from the UBO within the Vulkan
    // per-material set, so it takes 4. This mirrors basic_material so the
    // two unlit materials share the same slot shape.
    constexpr uint32_t draw_model_binding = 1;
    constexpr uint32_t material_params_binding = 3;
    constexpr uint32_t material_sprite_binding = 4;

    // std140 layout for the per-material params UBO: vec4 color at
    // offset 0, then float size, sizeAttenuation, useTexture at
    // offsets 16/20/24. The struct rounds up to 32 bytes.
    constexpr size_t material_ubo_size = 32;

    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 color;

        layout(location = 0) out vec3 pointColor;

        layout(set = 0, binding = 0, std140) uniform PerFrame
        {
            mat4 viewMatrix;
            mat4 projectionMatrix;
        } u_frame;

        layout(set = 1, binding = 1, std140) uniform PerDraw
        {
            mat4 modelMatrix;
        } u_draw;

        layout(set = 2, binding = 3, std140) uniform Material
        {
            vec4 color;
            float size;
            float sizeAttenuation;
            float useTexture;
        } u_material;

        void main()
        {
            pointColor = color;
            vec4 viewPosition = u_frame.viewMatrix * u_draw.modelMatrix * vec4(position, 1.0);
            gl_Position = u_frame.projectionMatrix * viewPosition;

            // View space looks down -z, so -viewPosition.z is the
            // forward distance. With attenuation the size is the sprite
            // diameter at one unit of depth and shrinks with distance;
            // without it the size is a constant pixel diameter.
            float depth = max(-viewPosition.z, 0.0001);
            gl_PointSize = u_material.sizeAttenuation != 0.0
                ? u_material.size / depth
                : u_material.size;
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) in vec3 pointColor;
        layout(location = 0) out vec4 fragColor;

        layout(set = 2, binding = 3, std140) uniform Material
        {
            vec4 color;
            float size;
            float sizeAttenuation;
            float useTexture;
        } u_material;

        layout(set = 2, binding = 4) uniform sampler2D spriteTexture;

        void main()
        {
            vec4 base = u_material.color * vec4(pointColor, 1.0);
            if (u_material.useTexture != 0.0)
            {
                // gl_PointCoord runs [0,1] across the rasterized point
                // sprite, so a sprite texture maps straight onto it.
                base *= texture(spriteTexture, gl_PointCoord);
            }
            fragColor = base;
        }
)fs";
} // namespace

namespace rendering_engine
{
    points_material::points_material(gpu::bind_group_layout frame_layout)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — the per-point stride is supplied at
        // set_vertex_buffer time by the renderable. Position sits at
        // offset 0 and the per-point colour at offset 12.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 3, gpu::scalar_type::float32, sizeof(float) * 3});

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching the points renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({draw_model_binding, gpu::binding_kind::uniform_buffer});

        // Per-material layout (slot 2): the params UBO plus the sprite
        // sampler, both owned by this material.
        gpu::bind_group_layout_descriptor material_layout{};
        material_layout.entries.push_back({material_params_binding, gpu::binding_kind::uniform_buffer});
        material_layout.entries.push_back({material_sprite_binding, gpu::binding_kind::texture});

        // Opaque unlit sprites: depth tested and written, no blending.
        // Particle-style additive blends are opt-in via set_color's
        // alpha plus a transparent material in future callers.
        material_params params{};
        params.transparent = false;
        params.depth_test = true;
        params.depth_write = true;

        construct_pipeline(vertex_shader,
                           fragment_shader,
                           vertex_layout,
                           draw_layout,
                           frame_layout,
                           params,
                           material_layout,
                           gpu::primitive_topology::points);

        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        auto& gpu = *control::current_engine().gpu;
        m_material_ubo = gpu.create_buffer(ubo_descriptor);

        rebuild_bind_group();
    }

    points_material::~points_material()
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
        if (m_sprite.valid())
        {
            gpu.destroy(m_sprite);
            m_sprite = {};
        }
        if (m_material_ubo.valid())
        {
            gpu.destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    uint32_t points_material::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    void points_material::set_color(const util::color& color)
    {
        m_color = color;
        upload_params();
    }

    void points_material::set_size(float size)
    {
        m_size = size;
        upload_params();
    }

    void points_material::set_size_attenuation(bool enabled)
    {
        m_size_attenuation = enabled;
        upload_params();
    }

    void points_material::set_sprite(const util::image& image)
    {
        auto& gpu = *control::current_engine().gpu;
        if (m_sprite.valid())
        {
            gpu.destroy(m_sprite);
            m_sprite = {};
        }

        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = gpu::texture_format::rgba8_unorm;
        descriptor.width = image.get_width();
        descriptor.height = image.get_height();
        descriptor.mipmaps = true;
        descriptor.min_filter = gpu::filter_mode::linear;
        descriptor.mag_filter = gpu::filter_mode::linear;
        descriptor.address_u = gpu::address_mode::clamp_edge;
        descriptor.address_v = gpu::address_mode::clamp_edge;
        descriptor.address_w = gpu::address_mode::clamp_edge;
        m_sprite = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
        gpu.write_texture(m_sprite, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(m_sprite);

        rebuild_bind_group();
    }

    void points_material::clear_sprite()
    {
        if (!m_sprite.valid())
        {
            return;
        }
        auto& gpu = *control::current_engine().gpu;
        gpu.destroy(m_sprite);
        m_sprite = {};
        rebuild_bind_group();
    }

    void points_material::rebuild_bind_group()
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

        gpu::binding_value sprite_slot{};
        sprite_slot.binding = material_sprite_binding;
        sprite_slot.kind = gpu::binding_kind::texture;
        sprite_slot.texture_value = m_sprite;
        bg_descriptor.entries.push_back(sprite_slot);

        m_per_material_bind_group = gpu.create_bind_group(bg_descriptor);

        upload_params();
    }

    void points_material::upload_params()
    {
        std::array<float, 8> payload{};
        payload[0] = static_cast<float>(m_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_color.a) / 255.0f;
        payload[4] = m_size;
        payload[5] = m_size_attenuation ? 1.0f : 0.0f;
        payload[6] = m_sprite.valid() ? 1.0f : 0.0f;

        auto& gpu = *control::current_engine().gpu;
        gpu.write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
