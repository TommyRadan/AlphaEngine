// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/tonemap_pass.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>
#include <runtime/engine.hpp>

namespace
{
    // The curve selection and the gamma-2.2 encode are
    // shaders/passes/tonemap.frag.glsl, drawn over the shared fullscreen
    // triangle. u_tonemap.op matches the rendering_engine::tonemap_operator
    // enumerator values exactly: 0 = none (clamp only), 1 = Reinhard,
    // 2 = ACES filmic.

    // std140 rounds the { float, int, float } block up to the 16-byte
    // minimum; the exposure sits at offset 0, the operator at offset 4 and
    // the grading intensity at offset 8.
    constexpr std::size_t tonemap_ubo_size = 16;

    std::array<std::byte, tonemap_ubo_size>
    pack_uniforms(float exposure, rendering_engine::tonemap_operator op, float grading_intensity)
    {
        std::array<std::byte, tonemap_ubo_size> bytes{};
        const std::int32_t op_value = static_cast<std::int32_t>(op);
        std::memcpy(bytes.data(), &exposure, sizeof(float));
        std::memcpy(bytes.data() + sizeof(float), &op_value, sizeof(std::int32_t));
        std::memcpy(bytes.data() + 2 * sizeof(float), &grading_intensity, sizeof(float));
        return bytes;
    }
} // namespace

namespace rendering_engine
{
    tonemap_pass::tonemap_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;

        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);

        // One fragment stage per keyword combination (see the variant_*
        // bits): an effect that is off is compiled out rather than
        // branched around, so it costs nothing.
        for (size_t variant = 0; variant < variant_count; ++variant)
        {
            gpu::shader_variant fragment{"passes/tonemap.frag.glsl", {}};
            if ((variant & variant_grading) != 0)
            {
                fragment.defines.emplace_back("USE_COLOR_GRADING", "1");
            }
            if ((variant & variant_auto_exposure) != 0)
            {
                fragment.defines.emplace_back("USE_AUTO_EXPOSURE", "1");
            }
            m_fragment_shaders[variant] = gpu::create_library_shader_module(gpu, fragment, gpu::shader_stage::fragment);
        }

        // Three vec2 vertices for the oversized fullscreen triangle.
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // The Tonemap UBO holds the exposure scale, the operator selector
        // and the grading blend. The first two are live-tunable through
        // set_exposure / set_operator, the blend through
        // frame_context::post; each change rewrites this buffer via
        // write_buffer. std140 lays the three out contiguously and rounds
        // the block up to the 16-byte minimum.
        const std::array<std::byte, tonemap_ubo_size> initial =
            pack_uniforms(m_exposure, m_operator, m_grading_intensity);
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = initial.size();
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::static_data;
        ubo_descriptor.initial_data = initial.data();
        m_tonemap_ubo = gpu.create_buffer(ubo_descriptor);

        // Every variant shares one layout; a variant that compiles a
        // sampler out simply never reads its slot.
        gpu::bind_group_layout_descriptor input_layout{};
        input_layout.entries.push_back({0, gpu::binding_kind::texture});
        input_layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        input_layout.entries.push_back({2, gpu::binding_kind::texture});
        input_layout.entries.push_back({3, gpu::binding_kind::texture});
        m_input_layout = gpu.create_bind_group_layout(input_layout);

        // The input bind group is built lazily by record(): the HDR image,
        // the grading LUT and the exposure texture it samples arrive
        // through the frame context and are rebound whenever a handle
        // changes.

        // Fullscreen triangle: depth disabled, blend disabled, no
        // culling so the triangle's winding is irrelevant. The
        // vertex shader reads a single vec2 attribute from
        // @ref m_vertex_buffer.
        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = sizeof(float) * 2;
        vertex_layout.attributes.push_back({0, 2, gpu::scalar_type::float32, 0});

        gpu::depth_state depth{};
        depth.test_enabled = false;
        depth.write_enabled = false;
        depth.compare = gpu::compare_function::always;

        gpu::blend_state blend{};
        blend.enabled = false;

        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = gpu::cull_mode::none;
        rasterizer.front = gpu::front_face::counter_clockwise;
        rasterizer.polygon = gpu::polygon_mode::fill;

        for (size_t variant = 0; variant < variant_count; ++variant)
        {
            gpu::pipeline_descriptor pipeline_descriptor{};
            pipeline_descriptor.vertex_shader = m_vertex_shader;
            pipeline_descriptor.fragment_shader = m_fragment_shaders[variant];
            pipeline_descriptor.vertex_buffers.push_back(vertex_layout);
            pipeline_descriptor.depth = depth;
            pipeline_descriptor.blend = blend;
            pipeline_descriptor.rasterizer = rasterizer;
            pipeline_descriptor.bind_group_layouts.push_back(m_input_layout);
            m_pipelines[variant] = gpu.create_pipeline(pipeline_descriptor);
        }
    }

    tonemap_pass::~tonemap_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;
        for (auto& pipeline : m_pipelines)
        {
            if (pipeline.valid())
            {
                gpu.destroy(pipeline);
                pipeline = {};
            }
        }
        if (m_input_bind_group.valid())
        {
            gpu.destroy(m_input_bind_group);
            m_input_bind_group = {};
        }
        if (m_input_layout.valid())
        {
            gpu.destroy(m_input_layout);
            m_input_layout = {};
        }
        if (m_tonemap_ubo.valid())
        {
            gpu.destroy(m_tonemap_ubo);
            m_tonemap_ubo = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        for (auto& fragment_shader : m_fragment_shaders)
        {
            if (fragment_shader.valid())
            {
                gpu.destroy(fragment_shader);
                fragment_shader = {};
            }
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
    }

    void tonemap_pass::rebuild_bind_group(gpu::texture input_color, gpu::texture grading_lut, gpu::texture exposure)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Safe mid-frame: the device defers the destroy until the command
        // buffer that may still reference the old group has retired.
        if (m_input_bind_group.valid())
        {
            gpu.destroy(m_input_bind_group);
            m_input_bind_group = {};
        }

        gpu::bind_group_descriptor input_bind_group_descriptor{};
        input_bind_group_descriptor.layout = m_input_layout;

        gpu::binding_value scene_color_slot{};
        scene_color_slot.binding = 0;
        scene_color_slot.kind = gpu::binding_kind::texture;
        scene_color_slot.texture_value = input_color;
        input_bind_group_descriptor.entries.push_back(scene_color_slot);

        gpu::binding_value tonemap_slot{};
        tonemap_slot.binding = 1;
        tonemap_slot.kind = gpu::binding_kind::uniform_buffer;
        tonemap_slot.buffer_value = m_tonemap_ubo;
        input_bind_group_descriptor.entries.push_back(tonemap_slot);

        // Either may be invalid when the variant drawn compiles its
        // sampler out: Vulkan then fills the slot with the device's 1x1
        // placeholder and OpenGL leaves the unit alone, and the shader
        // never reads it.
        gpu::binding_value grading_slot{};
        grading_slot.binding = 2;
        grading_slot.kind = gpu::binding_kind::texture;
        grading_slot.texture_value = grading_lut;
        input_bind_group_descriptor.entries.push_back(grading_slot);

        gpu::binding_value exposure_slot{};
        exposure_slot.binding = 3;
        exposure_slot.kind = gpu::binding_kind::texture;
        exposure_slot.texture_value = exposure;
        input_bind_group_descriptor.entries.push_back(exposure_slot);

        m_input_bind_group = gpu.create_bind_group(input_bind_group_descriptor);
        m_bound_input = input_color;
        m_bound_grading_lut = grading_lut;
        m_bound_exposure = exposure;
    }

    void tonemap_pass::prepare(const frame_context& ctx)
    {
        // Pick the variant: grading only with a table and a visible blend,
        // eye adaptation only while the auto-exposure pass publishes a
        // result. A texture the variant does not sample is left unbound
        // (invalid) so toggling one effect never rebinds for the other.
        const bool grading = ctx.grading_lut_texture.valid() && ctx.post.grading.intensity > 0.0f;
        const bool auto_exposure = ctx.exposure_texture.valid();
        m_variant = (grading ? variant_grading : 0) | (auto_exposure ? variant_auto_exposure : 0);
        const gpu::texture grading_lut = grading ? ctx.grading_lut_texture : gpu::texture{};
        const gpu::texture exposure = auto_exposure ? ctx.exposure_texture : gpu::texture{};

        if (grading && ctx.post.grading.intensity != m_grading_intensity)
        {
            m_grading_intensity = ctx.post.grading.intensity;
            upload_uniforms();
        }

        // Bind this frame's HDR image and the optional inputs. The handles
        // only change when a target is recreated (a resize), motion blur
        // is toggled, the LUT is swapped or auto exposure is toggled, so
        // compare against the ones the bind group was built with and
        // rebuild on change — the first frame included.
        if (ctx.hdr_color_texture != m_bound_input || grading_lut != m_bound_grading_lut ||
            exposure != m_bound_exposure || !m_input_bind_group.valid())
        {
            rebuild_bind_group(ctx.hdr_color_texture, grading_lut, exposure);
        }
    }

    void tonemap_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        gpu::render_pass_descriptor descriptor{};
        // Resolve into the off-screen LDR target rather than straight to
        // the swapchain: the final post effect (FXAA) needs to sample
        // this tonemapped result as a shader input, which the swapchain
        // cannot provide.
        descriptor.target = ctx.ldr_color_target;
        // The fullscreen triangle covers every pixel; clearing is
        // strictly redundant but cheap and keeps the target in a
        // known state if a future post pass narrows its viewport.
        descriptor.color[0].load = gpu::load_op::clear;
        descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_pipelines[m_variant]);
        pass_encoder->set_bind_group(0, m_input_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3);
        pass_encoder->end();
    }

    void tonemap_pass::set_exposure(float exposure)
    {
        if (exposure == m_exposure)
        {
            return;
        }
        m_exposure = exposure;
        upload_uniforms();
    }

    void tonemap_pass::set_operator(tonemap_operator op)
    {
        if (op == m_operator)
        {
            return;
        }
        m_operator = op;
        upload_uniforms();
    }

    void tonemap_pass::upload_uniforms()
    {
        auto& gpu = *runtime::current_engine().gpu;
        const std::array<std::byte, tonemap_ubo_size> bytes =
            pack_uniforms(m_exposure, m_operator, m_grading_intensity);
        gpu.write_buffer(m_tonemap_ubo, bytes.data(), bytes.size(), 0);
    }
} // namespace rendering_engine
