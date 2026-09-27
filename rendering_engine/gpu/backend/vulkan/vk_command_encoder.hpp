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

/**
 * @file vk_command_encoder.hpp
 * @brief Vulkan implementation of @ref gpu::command_encoder,
 *        @ref gpu::render_pass_encoder, @ref gpu::compute_pass_encoder.
 */

#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/command_encoder.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    struct vk_device;

    // One render pass on a primary command buffer, or one secondary
    // command buffer of a parallel pass (render_pass_descriptor::parallel).
    // A primary begun with @c parallel opens its render pass with
    // secondary-buffer contents: it records no draw itself (a draw-level
    // call on it is reported once and dropped), hands out secondaries
    // through begin_secondary — each a secondary command buffer from the
    // recording lane's pool, begun with the pass and framebuffer as
    // inheritance and the viewport / scissor a fresh pass sets, so any
    // thread can record its chunk while the main thread waits on the
    // fork — and splices them back in order with execute_secondary,
    // which is also where their draw tallies reach the device's frame
    // counters. A secondary's end() ends its command buffer.
    struct vk_render_pass_encoder : public render_pass_encoder
    {
        vk_render_pass_encoder(vk_device& device, VkCommandBuffer cmd, const render_pass_descriptor& descriptor);
        // A secondary of @p primary on @p cmd, a secondary command buffer
        // of the current frame slot; see begin_secondary.
        vk_render_pass_encoder(vk_device& device, VkCommandBuffer cmd, const vk_render_pass_encoder& primary);
        ~vk_render_pass_encoder() override;

        void set_pipeline(pipeline pipeline_handle) override;
        void set_vertex_buffer(uint32_t slot, buffer buffer_handle, size_t offset, uint32_t stride_override) override;
        void set_index_buffer(buffer buffer_handle, index_format format) override;
        void set_bind_group(uint32_t group,
                            bind_group bind_group_handle,
                            std::span<const uint32_t> dynamic_offsets) override;
        // vkCmdPushConstants against the bound pipeline's layout, once
        // the bytes are found inside one of its declared ranges with the
        // same stages (the validation layer's rule, checked here so a
        // bad push is reported once instead of failing validation).
        void push_constants(shader_stages stages, uint32_t offset, uint32_t size, const void* data) override;
        // @p x / @p y are the rectangle's bottom-left corner in
        // window (OpenGL) convention, matching the GL backend's
        // glViewport / glScissor. A swapchain pass renders through a
        // negative-height viewport, so both the viewport and the
        // scissor are flipped into Vulkan's top-left framebuffer
        // space here; off-screen passes keep Vulkan's orientation.
        void set_viewport(int x, int y, int width, int height) override;
        void set_scissor(int x, int y, int width, int height) override;
        void set_stencil_reference(uint32_t reference) override;
        void
        draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) override;
        void draw_indexed(uint32_t index_count,
                          uint32_t instance_count,
                          uint32_t first_index,
                          int32_t base_vertex,
                          uint32_t first_instance) override;
        void draw_indexed_indirect(buffer indirect_buffer, size_t offset) override;
        void multi_draw_indexed_indirect(buffer indirect_buffer,
                                         size_t offset,
                                         uint32_t draw_count,
                                         uint32_t stride) override;
        // Null unless a render pass is open: a pass that failed to
        // open (no swapchain image this frame — minimised, or the
        // acquire failed) must not hand out the command buffer, or an
        // overlay would record draws outside any render pass. A primary
        // whose pass takes secondaries hands out nothing either: nothing
        // may be recorded inline into it.
        void* native_command_buffer() const noexcept override
        {
            return m_in_pass && !m_secondary_contents ? static_cast<void*>(m_cmd) : nullptr;
        }
        // Null unless a render pass is open; see
        // @c render_pass_encoder::native_render_pass.
        void* native_render_pass() const noexcept override
        {
            return m_in_pass ? static_cast<void*>(m_render_pass) : nullptr;
        }
        std::unique_ptr<render_pass_encoder> begin_secondary(uint32_t lane) override;
        void execute_secondary(render_pass_encoder& secondary) override;
        void end() override;

    private:
        // Whether a draw-level call may be recorded on this encoder: the
        // pass (or the secondary's buffer) is open and, for a primary,
        // its draws are not delegated to secondaries. The first call
        // refused for the latter reason is reported.
        bool inline_recording();

        // Counts a draw for the device's per-frame diagnostics: on a
        // primary straight away, on a secondary into m_tally, which the
        // primary merges when it executes the secondary (the device's
        // counters are the main thread's).
        void tally_draw(uint32_t vertex_count);
        void tally_draw_indexed(uint32_t index_count);

        vk_device& m_device;
        VkCommandBuffer m_cmd{VK_NULL_HANDLE};
        VkRenderPass m_render_pass{VK_NULL_HANDLE};
        // The framebuffer the pass was begun with; a secondary inherits
        // it and a primary checks a secondary against it.
        VkFramebuffer m_framebuffer{VK_NULL_HANDLE};
        // Generation of m_render_pass (see vk_render_target::variant);
        // part of the pipeline-cache key passed to graphics_pipeline_for.
        uint64_t m_render_pass_generation{0};
        // What a pipeline built against the open pass must match: the
        // colour attachments it blends into and the sample count.
        uint32_t m_color_count{0};
        VkSampleCountFlagBits m_samples{VK_SAMPLE_COUNT_1_BIT};
        uint32_t m_target_width{0};
        uint32_t m_target_height{0};
        pipeline m_pipeline_handle{};
        VkPipelineLayout m_current_pipeline_layout{VK_NULL_HANDLE};
        bool m_in_pass{false};
        // True only when the active render pass writes to the
        // swapchain. The encoder applies a negative-height viewport
        // and asks for a Y-flipped pipeline variant in this mode;
        // off-screen targets render in Vulkan-natural orientation
        // so a downstream sampler (tonemap) sees its texels at the
        // texCoord origin the OpenGL-style shader expects.
        bool m_y_flipped{false};
        // The dynamic stencil reference: every graphics pipeline
        // declares it dynamic, so it is supplied after each bind and
        // whenever set_stencil_reference changes it.
        uint32_t m_stencil_reference{0};
        // Bind-group handles set_bind_group has already reported as
        // not live, so a broken handle logs once per pass rather than
        // once per draw.
        std::vector<uint64_t> m_reported_bind_groups;
        // Set once push_constants has reported a push the bound
        // pipeline does not declare, so it logs once per pass.
        bool m_push_constants_reported{false};

        // This encoder records a secondary command buffer of a parallel
        // pass (the second constructor) rather than a render pass on a
        // primary.
        bool m_secondary{false};
        // This primary's pass was begun with render_pass_descriptor::
        // parallel: its draws come from secondaries and inline
        // recording is refused (reported once, m_inline_reported).
        bool m_secondary_contents{false};
        bool m_inline_reported{false};
        // A secondary's buffer has been ended, and has been executed by
        // its primary; each may happen once.
        bool m_ended{false};
        bool m_executed{false};
        // A secondary's draws, merged into the device's frame counters
        // when the primary executes it.
        struct draw_tally
        {
            uint32_t draws{0};
            uint32_t vertices{0};
            uint32_t draws_indexed{0};
            uint32_t indices{0};
        };
        draw_tally m_tally{};
    };

    struct vk_compute_pass_encoder : public compute_pass_encoder
    {
        vk_compute_pass_encoder(vk_device& device, VkCommandBuffer cmd);
        ~vk_compute_pass_encoder() override;

        void set_pipeline(pipeline pipeline_handle) override;
        void set_bind_group(uint32_t group,
                            bind_group bind_group_handle,
                            std::span<const uint32_t> dynamic_offsets) override;
        void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override;
        void end() override;

    private:
        vk_device& m_device;
        VkCommandBuffer m_cmd{VK_NULL_HANDLE};
        VkPipelineLayout m_current_pipeline_layout{VK_NULL_HANDLE};
        bool m_active{false};
        // Storage-image textures moved to VK_IMAGE_LAYOUT_GENERAL while
        // bound for compute writes. Restored to the sampled layout in
        // end() so later passes read them as combined image samplers.
        // Each image is tracked once (the device reports whether it
        // actually transitioned), even when bound across several
        // dispatches.
        std::vector<texture> m_storage_textures;
        // See vk_render_pass_encoder::m_reported_bind_groups.
        std::vector<uint64_t> m_reported_bind_groups;
    };

    struct vk_command_encoder : public command_encoder
    {
        explicit vk_command_encoder(vk_device& device);
        ~vk_command_encoder() override;

        std::unique_ptr<render_pass_encoder> begin_render_pass(const render_pass_descriptor& descriptor) override;
        std::unique_ptr<compute_pass_encoder> begin_compute_pass() override;
        void copy_buffer_to_buffer(buffer src, size_t src_offset, buffer dst, size_t dst_offset, size_t size) override;
        void clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value) override;
        void barrier(pipeline_stage src_stage,
                     pipeline_stage dst_stage,
                     access_flag src_access,
                     access_flag dst_access) override;
        void
        copy_buffer_to_texture(buffer src, size_t src_offset, texture dst, const texture_copy_region& region) override;
        void
        copy_texture_to_buffer(texture src, const texture_copy_region& region, buffer dst, size_t dst_offset) override;
        void push_debug_group(const char* name) override;
        void pop_debug_group() override;
        void reset_queries(query_set set, uint32_t first, uint32_t count) override;
        void write_timestamp(query_set set, uint32_t index) override;

        // Hand the recorded command buffer over to @c vk_device::submit.
        // Returns the buffer and clears the encoder's reference. The
        // buffer belongs to the device's frame command pool either way
        // (see vk_device::acquire_frame_command_buffer); the encoder
        // only records into it.
        VkCommandBuffer release_command_buffer() noexcept;

    private:
        vk_device& m_device;
        VkCommandBuffer m_cmd{VK_NULL_HANDLE};
        bool m_began{false};
        // Open debug labels, so a pop never underflows.
        uint32_t m_debug_label_depth{0};
    };
} // namespace rendering_engine::gpu::backend::vulkan
