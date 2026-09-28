// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/ui_pass.hpp>

#include <algorithm>
#include <functional>

#include <core/math/mat4.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/materials/ui_material.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

namespace rendering_engine
{
    namespace
    {
        // std140 layout of the UiFrame block (shaders/materials/ui.vert.glsl):
        // mat4 projection at 0, vec4 viewport at 64 (size in pixels, then
        // its reciprocal). 80 bytes.
        struct ui_frame_block
        {
            core::math::mat4 projection;
            float viewport[4];
        };

        static_assert(sizeof(ui_frame_block) == 80, "UiFrame block must be a std140 mat4 and a vec4");
    } // namespace

    ui_pass::ui_pass(gpu::device& device) : m_device(&device)
    {
        m_frame_layout = m_device->create_bind_group_layout(ui_material::frame_layout_descriptor());
    }

    ui_pass::~ui_pass()
    {
        if (m_frame_layout.valid())
        {
            m_device->destroy(m_frame_layout);
        }
    }

    gpu::bind_group_layout ui_pass::frame_bind_group_layout() const
    {
        return m_frame_layout;
    }

    ui_pass::view_data::view_data(gpu::device& device, gpu::bind_group_layout frame_layout) : device{&device}
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sizeof(ui_frame_block);
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        frame_ubo = device.create_buffer(ubo_descriptor);

        gpu::bind_group_descriptor bind_group_descriptor{};
        bind_group_descriptor.layout = frame_layout;
        gpu::binding_value frame_slot{};
        frame_slot.binding = ui_material::frame_binding;
        frame_slot.kind = gpu::binding_kind::uniform_buffer;
        frame_slot.buffer_value = frame_ubo;
        bind_group_descriptor.entries.push_back(frame_slot);
        frame_bind_group = device.create_bind_group(bind_group_descriptor);
    }

    ui_pass::view_data::~view_data()
    {
        if (frame_bind_group.valid())
        {
            device->destroy(frame_bind_group);
        }
        if (frame_ubo.valid())
        {
            device->destroy(frame_ubo);
        }
    }

    void ui_pass::view_data::write_frame_block(uint32_t new_width, uint32_t new_height)
    {
        // A view is at least one pixel across; the clamp keeps the
        // reciprocal finite regardless.
        const float block_width = static_cast<float>(std::max<uint32_t>(new_width, 1));
        const float block_height = static_cast<float>(std::max<uint32_t>(new_height, 1));

        // Left 0, right width; bottom height, top 0: pixel rows grow
        // downwards and land on the engine's y-up clip space (the Vulkan
        // backend flips its swapchain viewport to keep NDC +Y up).
        ui_frame_block block{};
        block.projection = core::math::ortho(0.0f, block_width, block_height, 0.0f, -1.0f, 1.0f);
        block.viewport[0] = block_width;
        block.viewport[1] = block_height;
        block.viewport[2] = 1.0f / block_width;
        block.viewport[3] = 1.0f / block_height;
        device->write_buffer(frame_ubo, &block, sizeof(block), 0);
        width = new_width;
        height = new_height;
    }

    void ui_pass::prepare(const frame_context& ctx)
    {
        // Inside the frame bracket: the frame that last read the view's
        // block has retired, so a new projection can be written here.
        view_data& view = ctx.view->state<view_data>(*this, *m_device, m_frame_layout);
        if (view.width != ctx.viewport_width || view.height != ctx.viewport_height)
        {
            view.write_frame_block(ctx.viewport_width, ctx.viewport_height);
        }
        m_frame_bind_group = view.frame_bind_group;

        m_output = ctx.resources->get(frame_resources::output);
        m_items.assign(ctx.ui_draws.begin(), ctx.ui_draws.end());
        // Sorted by (pipeline, material instance) so instances sharing
        // a pipeline sit together; the per-material group is rebound
        // when the instance changes, not only when the pipeline does.
        // The sort is stable, so the draws of one material keep the
        // paint order: an element whose proxy was created later paints
        // on top.
        std::stable_sort(m_items.begin(),
                         m_items.end(),
                         [](const draw_item& a, const draw_item& b)
                         {
                             const uint64_t pipeline_a = a.mat->pipeline().id;
                             const uint64_t pipeline_b = b.mat->pipeline().id;
                             if (pipeline_a != pipeline_b)
                             {
                                 return pipeline_a < pipeline_b;
                             }
                             return std::less<const material*>{}(a.mat, b.mat);
                         });
    }

    void ui_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_output.target;
        // The view's post chain already drew its rectangle (over a cleared
        // black image when there was no camera); either way the UI overlay
        // is drawn on top without re-clearing the colour, and depth is
        // disabled so the overlay always wins.
        descriptor.color[0].load = gpu::load_op::load;
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        if (!m_output.covers_target())
        {
            pass_encoder->set_viewport(
                m_output.x, m_output.y, static_cast<int>(m_output.width), static_cast<int>(m_output.height));
        }

        uint64_t last_pipeline_id = 0;
        const material* last_material = nullptr;
        for (const auto& item : m_items)
        {
            const uint64_t pid = item.mat->pipeline().id;
            if (pid != last_pipeline_id)
            {
                pass_encoder->set_pipeline(item.mat->pipeline());
                // Every pipeline built on the ui template reads the
                // UiFrame group at slot 0; a material with some other
                // per-frame layout gets nothing bound there by this pass.
                if (item.mat->get_template().descriptor().frame_layout == m_frame_layout)
                {
                    pass_encoder->set_bind_group(0, m_frame_bind_group);
                }
                last_pipeline_id = pid;
                last_material = nullptr;
            }
            if (item.mat != last_material)
            {
                if (item.mat->per_material_bind_group().valid())
                {
                    pass_encoder->set_bind_group(item.mat->per_material_slot(), item.mat->per_material_bind_group());
                }
                last_material = item.mat;
            }

            // The per-draw data pushed or bound, as in the scene pass: a
            // quad group's texture group here; a draw without per-draw
            // resources records nothing.
            bind_per_draw(*pass_encoder, item, item.mat->per_draw_slot());
            pass_encoder->set_vertex_buffer(0, item.vertex_buffer, 0, item.vertex_stride);
            if (item.index_buffer.valid())
            {
                pass_encoder->set_index_buffer(item.index_buffer, item.index_format);
                pass_encoder->draw_indexed(item.index_count, item.instance_count, item.first_index, item.vertex_offset);
            }
            else
            {
                pass_encoder->draw(item.vertex_count, item.instance_count, static_cast<uint32_t>(item.vertex_offset));
            }
        }

        pass_encoder->end();
    }
} // namespace rendering_engine
