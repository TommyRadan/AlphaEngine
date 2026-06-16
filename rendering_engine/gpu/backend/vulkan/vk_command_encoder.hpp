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

    struct vk_render_pass_encoder : public render_pass_encoder
    {
        vk_render_pass_encoder(vk_device& device, VkCommandBuffer cmd, const render_pass_descriptor& descriptor);
        ~vk_render_pass_encoder() override;

        void set_pipeline(pipeline pipeline_handle) override;
        void set_vertex_buffer(uint32_t slot, buffer buffer_handle, size_t offset, uint32_t stride_override) override;
        void set_index_buffer(buffer buffer_handle, index_format format) override;
        void set_bind_group(uint32_t group, bind_group bind_group_handle) override;
        void set_viewport(int x, int y, int width, int height) override;
        void draw(uint32_t vertex_count, uint32_t first_vertex) override;
        void draw_indexed(uint32_t index_count, uint32_t first_index) override;
        void draw_indexed_indirect(buffer indirect_buffer, size_t offset) override;
        void multi_draw_indexed_indirect(buffer indirect_buffer,
                                         size_t offset,
                                         uint32_t draw_count,
                                         uint32_t stride) override;
        void* native_command_buffer() const noexcept override
        {
            return m_cmd;
        }
        void end() override;

    private:
        vk_device& m_device;
        VkCommandBuffer m_cmd{VK_NULL_HANDLE};
        VkRenderPass m_render_pass{VK_NULL_HANDLE};
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
    };

    struct vk_compute_pass_encoder : public compute_pass_encoder
    {
        vk_compute_pass_encoder(vk_device& device, VkCommandBuffer cmd);
        ~vk_compute_pass_encoder() override;

        void set_pipeline(pipeline pipeline_handle) override;
        void set_bind_group(uint32_t group, bind_group bind_group_handle) override;
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

        // Hand the recorded command buffer over to @c vk_device::submit.
        // Returns the buffer and clears the encoder's reference so the
        // destructor leaves it in flight rather than freeing it.
        VkCommandBuffer release_command_buffer() noexcept;

    private:
        vk_device& m_device;
        VkCommandBuffer m_cmd{VK_NULL_HANDLE};
        bool m_began{false};
    };
} // namespace rendering_engine::gpu::backend::vulkan
