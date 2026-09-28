// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/fxaa_pass.hpp>

#include <array>
#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>

namespace
{
    // The filter itself is shaders/passes/fxaa.frag.glsl, drawn over the
    // shared fullscreen triangle. u_fxaa.rcp_frame.xy is (1/width,
    // 1/height), the per-texel step the edge search walks by, baked per
    // view from the view's size.

    // std140 rounds the vec2 up to a 16-byte vec4 allocation.
    constexpr size_t rcp_frame_ubo_size = 16;

    // {1/width, 1/height, 0, 0}; a zero dimension gives a zero step.
    std::array<float, 4> rcp_frame(uint32_t width, uint32_t height)
    {
        const float rcp_x = (width != 0) ? 1.0f / static_cast<float>(width) : 0.0f;
        const float rcp_y = (height != 0) ? 1.0f / static_cast<float>(height) : 0.0f;
        return {rcp_x, rcp_y, 0.0f, 0.0f};
    }
} // namespace

namespace rendering_engine
{
    fxaa_pass::fxaa_pass(gpu::device& device) : m_device(&device)
    {
        auto& gpu = *m_device;

        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
        m_fragment_shader =
            gpu::create_library_shader_module(gpu, "passes/fxaa.frag.glsl", gpu::shader_stage::fragment);

        // Three vec2 vertices for the oversized fullscreen triangle.
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        gpu::bind_group_layout_descriptor input_layout{};
        input_layout.entries.push_back({0, gpu::binding_kind::texture});
        input_layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        m_input_layout = gpu.create_bind_group_layout(input_layout);

        // Each view's rcp_frame UBO and input bind groups are built lazily
        // by prepare(): the image it samples (the TAA resolve or the LDR
        // target) is looked up in the view's store and a group is built the
        // first time a handle is seen.

        // Fullscreen triangle: depth disabled, blend disabled, no culling
        // so the triangle's winding is irrelevant. The vertex shader reads
        // a single vec2 attribute from @ref m_vertex_buffer.
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

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = m_vertex_shader;
        pipeline_descriptor.fragment_shader = m_fragment_shader;
        pipeline_descriptor.vertex_buffers.push_back(vertex_layout);
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        pipeline_descriptor.bind_group_layouts.push_back(m_input_layout);

        m_pipeline = gpu.create_pipeline(pipeline_descriptor);
    }

    fxaa_pass::~fxaa_pass()
    {
        auto& gpu = *m_device;
        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        if (m_input_layout.valid())
        {
            gpu.destroy(m_input_layout);
            m_input_layout = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        if (m_fragment_shader.valid())
        {
            gpu.destroy(m_fragment_shader);
            m_fragment_shader = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
    }

    fxaa_pass::view_data::view_data(gpu::device& device, uint32_t width, uint32_t height)
        : device{&device}, baked_width{width}, baked_height{height}
    {
        const std::array<float, 4> initial = rcp_frame(width, height);
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = rcp_frame_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::static_data;
        ubo_descriptor.initial_data = initial.data();
        rcp_frame_ubo = device.create_buffer(ubo_descriptor);
    }

    fxaa_pass::view_data::~view_data()
    {
        for (auto& input : inputs)
        {
            if (input.bind_group.valid())
            {
                device->destroy(input.bind_group);
            }
        }
        if (rcp_frame_ubo.valid())
        {
            device->destroy(rcp_frame_ubo);
        }
    }

    gpu::bind_group fxaa_pass::bind_group_for(view_data& view, gpu::texture input_color)
    {
        for (const auto& input : view.inputs)
        {
            if (input.bind_group.valid() && input.texture == input_color)
            {
                return input.bind_group;
            }
        }

        auto& gpu = *m_device;

        // Miss: build into the rotating slot. Releasing what it held is
        // safe mid-frame — the device defers the destroy until the command
        // buffer that may still reference the old group has retired — and
        // with two slots the TAA ping-pong pair stays resident while a
        // resize's stale handles rotate out over two frames.
        bound_input& slot = view.inputs[view.next_input_slot];
        view.next_input_slot = (view.next_input_slot + 1) % view.inputs.size();
        if (slot.bind_group.valid())
        {
            gpu.destroy(slot.bind_group);
            slot = {};
        }

        gpu::bind_group_descriptor input_bind_group_descriptor{};
        input_bind_group_descriptor.layout = m_input_layout;

        gpu::binding_value ldr_color_slot{};
        ldr_color_slot.binding = 0;
        ldr_color_slot.kind = gpu::binding_kind::texture;
        ldr_color_slot.texture_value = input_color;
        input_bind_group_descriptor.entries.push_back(ldr_color_slot);

        gpu::binding_value rcp_frame_slot{};
        rcp_frame_slot.binding = 1;
        rcp_frame_slot.kind = gpu::binding_kind::uniform_buffer;
        rcp_frame_slot.buffer_value = view.rcp_frame_ubo;
        input_bind_group_descriptor.entries.push_back(rcp_frame_slot);

        slot.bind_group = gpu.create_bind_group(input_bind_group_descriptor);
        slot.texture = input_color;
        return slot.bind_group;
    }

    void fxaa_pass::prepare(const frame_context& ctx)
    {
        // The edge step the view needs: its size, or a zero step while FXAA
        // is disabled, collapsing every tap onto the centre texel so this
        // pass — which always stays in the chain because it is what writes
        // the output — degrades to a straight copy instead of skipping the
        // draw. A new view bakes it into its UBO; a resize or an
        // fxaa.enabled flip rewrites it, now that begin_frame has waited for
        // the frame that may still have been reading the UBO. The bind
        // groups keep referencing the same buffer; only its contents
        // change.
        const uint32_t step_width = ctx.post.fxaa.enabled ? ctx.viewport_width : 0;
        const uint32_t step_height = ctx.post.fxaa.enabled ? ctx.viewport_height : 0;
        view_data& view = ctx.view->state<view_data>(*this, *m_device, step_width, step_height);
        if (view.baked_width != step_width || view.baked_height != step_height)
        {
            const std::array<float, 4> step = rcp_frame(step_width, step_height);
            m_device->write_buffer(view.rcp_frame_ubo, step.data(), rcp_frame_ubo_size, 0);
            view.baked_width = step_width;
            view.baked_height = step_height;
        }

        // Sample the TAA resolve when temporal AA produced one this frame,
        // otherwise the raw tonemap output. The resolve alternates between
        // the TAA pass's two ping-pong targets and either handle changes
        // when its target is recreated (a resize), so look the group up by
        // handle and build one on a miss — the first frame included.
        const gpu::texture resolve = ctx.resources->get(frame_resources::taa_resolve);
        const gpu::texture input = resolve.valid() ? resolve : ctx.resources->get(frame_resources::ldr_color).texture;
        m_input_bind_group = bind_group_for(view, input);
        m_output = ctx.resources->get(frame_resources::output);
    }

    void fxaa_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_output.target;
        // The first view to draw into the output clears all of it: the
        // fullscreen triangle covers every pixel of the view's rectangle,
        // and the clear leaves whatever no view covers in a known state. A
        // later view keeps what the views before it drew.
        descriptor.color[0].load = m_output.first ? gpu::load_op::clear : gpu::load_op::load;
        descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        if (!m_output.covers_target())
        {
            pass_encoder->set_viewport(
                m_output.x, m_output.y, static_cast<int>(m_output.width), static_cast<int>(m_output.height));
        }
        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_input_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3);
        pass_encoder->end();
    }
} // namespace rendering_engine
