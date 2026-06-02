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

#include <rendering_engine/materials/basic_material.hpp>

#include <array>
#include <string>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <runtime/engine.hpp>

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
    constexpr uint32_t material_albedo_binding = 4;

    // std140 layout for the per-material params UBO: vec4 color at
    // offset 0, float useTexture at offset 16. The struct rounds up to
    // 32 bytes.
    constexpr size_t material_ubo_size = 32;

    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;

        layout(location = 0) out vec2 texCoord;

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
            texCoord = uv;
            mat4 MVP = u_frame.projectionMatrix * u_frame.viewMatrix * u_draw.modelMatrix;
            gl_Position = MVP * vec4(position, 1.0);
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 2, binding = 3, std140) uniform Material
        {
            vec4 color;
            float useTexture;
        } u_material;

        layout(set = 2, binding = 4) uniform sampler2D albedoTexture;

        void main()
        {
            vec4 base = u_material.color;
            if (u_material.useTexture != 0.0)
            {
                base *= texture(albedoTexture, texCoord);
            }
            fragColor = base;
        }
)fs";
} // namespace

namespace rendering_engine
{
    basic_material::basic_material(gpu::bind_group_layout frame_layout)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — per-renderable strides are supplied at
        // set_vertex_buffer time. Position sits at offset 0 and UV at
        // offset 12 across every position+uv(+normal) vertex this
        // pipeline draws, so the baked attribute offsets stay valid.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 2, gpu::scalar_type::float32, sizeof(float) * 3});

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching every 3D renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({draw_model_binding, gpu::binding_kind::uniform_buffer});

        // Per-material layout (slot 2): the {color, useTexture} UBO plus
        // the albedo sampler, both owned by this material.
        gpu::bind_group_layout_descriptor material_layout{};
        material_layout.entries.push_back({material_params_binding, gpu::binding_kind::uniform_buffer});
        material_layout.entries.push_back({material_albedo_binding, gpu::binding_kind::texture});

        // Opaque unlit surface: depth tested and written, no blending.
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
        auto& gpu = *runtime::current_engine().gpu;
        m_material_ubo = gpu.create_buffer(ubo_descriptor);

        rebuild_bind_group();
    }

    basic_material::~basic_material()
    {
        // Drop the bind group before the buffers / texture it
        // references, then null it so the base destructor's
        // destruct_pipeline does not double-free.
        auto& gpu = *runtime::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
        if (m_albedo.valid())
        {
            gpu.destroy(m_albedo);
            m_albedo = {};
        }
        if (m_material_ubo.valid())
        {
            gpu.destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    uint32_t basic_material::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    void basic_material::set_color(const util::color& color)
    {
        m_color = color;
        upload_params();
    }

    void basic_material::set_albedo(const util::image& image)
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_albedo.valid())
        {
            gpu.destroy(m_albedo);
            m_albedo = {};
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
        m_albedo = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
        gpu.write_texture(m_albedo, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(m_albedo);

        rebuild_bind_group();
    }

    void basic_material::clear_albedo()
    {
        if (!m_albedo.valid())
        {
            return;
        }
        auto& gpu = *runtime::current_engine().gpu;
        gpu.destroy(m_albedo);
        m_albedo = {};
        rebuild_bind_group();
    }

    void basic_material::rebuild_bind_group()
    {
        auto& gpu = *runtime::current_engine().gpu;
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
        tex_slot.binding = material_albedo_binding;
        tex_slot.kind = gpu::binding_kind::texture;
        tex_slot.texture_value = m_albedo;
        bg_descriptor.entries.push_back(tex_slot);

        m_per_material_bind_group = gpu.create_bind_group(bg_descriptor);

        upload_params();
    }

    void basic_material::upload_params()
    {
        std::array<float, 8> payload{};
        payload[0] = static_cast<float>(m_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_color.a) / 255.0f;
        payload[4] = m_albedo.valid() ? 1.0f : 0.0f;

        auto& gpu = *runtime::current_engine().gpu;
        gpu.write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
