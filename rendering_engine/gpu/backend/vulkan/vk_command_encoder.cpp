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

#include <rendering_engine/gpu/backend/vulkan/vk_command_encoder.hpp>

#include <array>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    // -- vk_render_pass_encoder --------------------------------------

    namespace
    {
        // Vulkan rasterises with Y down by default; the engine's
        // projection matrices are GL-style (Y up). For the swapchain
        // path we use a negative-height viewport so OpenGL clip
        // space lands the right way round on screen. Off-screen
        // targets keep Vulkan's natural Y-down so that a downstream
        // sampler (tonemap) sees image row 0 == "world Z down" —
        // the texCoord origin the OpenGL-style shader assumes. The
        // matching front-face mapping happens at pipeline build
        // time, keyed off the same y_flipped flag.
        VkViewport make_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height, bool y_flipped)
        {
            VkViewport vp{};
            vp.x = static_cast<float>(x);
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;
            if (y_flipped)
            {
                vp.y = static_cast<float>(y + height);
                vp.width = static_cast<float>(width);
                vp.height = -static_cast<float>(height);
            }
            else
            {
                vp.y = static_cast<float>(y);
                vp.width = static_cast<float>(width);
                vp.height = static_cast<float>(height);
            }
            return vp;
        }
    } // namespace

    vk_render_pass_encoder::vk_render_pass_encoder(vk_device& device,
                                                   VkCommandBuffer cmd,
                                                   const render_pass_descriptor& descriptor)
        : m_device{device}, m_cmd{cmd}
    {
        auto* target = device.lookup_render_target(descriptor.target);
        if (target == nullptr || cmd == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_render_pass_encoder: missing %s", target == nullptr ? "render target" : "command buffer");
            return;
        }

        const VkAttachmentLoadOp color_load = to_vk_load_op(descriptor.color.load);
        const VkAttachmentLoadOp depth_load = to_vk_load_op(descriptor.depth.load);
        const bool use_depth = descriptor.use_depth && target->has_depth;
        VkRenderPass render_pass = device.acquire_render_pass(*target, color_load, depth_load, use_depth);
        if (render_pass == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_render_pass_encoder: acquire_render_pass returned null");
            return;
        }

        // Find the variant we just acquired so we can pick the
        // matching framebuffer. Each variant carries its own
        // framebuffer set since variants with different use_depth
        // are not render-pass compatible.
        const vk_render_target::variant* variant = nullptr;
        for (const auto& v : target->variants)
        {
            if (v.render_pass == render_pass)
            {
                variant = &v;
                break;
            }
        }
        if (variant == nullptr)
        {
            LOG_ERR("vk_render_pass_encoder: no matching variant for acquired render pass");
            return;
        }

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (target->is_swapchain)
        {
            device.begin_frame();
            if (!device.have_current_swapchain_image() || variant->framebuffers.empty())
            {
                LOG_ERR("vk_render_pass_encoder: no swapchain image acquired (have_image=%i framebuffers=%u)",
                        static_cast<int>(device.have_current_swapchain_image()),
                        static_cast<unsigned>(variant->framebuffers.size()));
                return;
            }
            const uint32_t idx = device.current_swapchain_image_index();
            if (idx < variant->framebuffers.size())
            {
                framebuffer = variant->framebuffers[idx];
            }
        }
        else
        {
            framebuffer = !variant->framebuffers.empty() ? variant->framebuffers.front() : VK_NULL_HANDLE;
        }
        if (framebuffer == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_render_pass_encoder: framebuffer is null");
            return;
        }

        m_render_pass = render_pass;
        m_target_width = target->width;
        m_target_height = target->height;
        m_y_flipped = target->is_swapchain;
        device.note_render_pass_opened(target->is_swapchain, use_depth);

        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{descriptor.color.clear_color[0],
                            descriptor.color.clear_color[1],
                            descriptor.color.clear_color[2],
                            descriptor.color.clear_color[3]}};
        clears[1].depthStencil = {descriptor.depth.clear_depth, 0};

        VkRenderPassBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bi.renderPass = render_pass;
        bi.framebuffer = framebuffer;
        bi.renderArea.offset = {0, 0};
        bi.renderArea.extent = {target->width, target->height};
        bi.clearValueCount = use_depth ? 2 : 1;
        bi.pClearValues = clears.data();
        vkCmdBeginRenderPass(m_cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport vp = make_viewport(0, 0, target->width, target->height, m_y_flipped);
        vkCmdSetViewport(m_cmd, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {target->width, target->height};
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
        m_in_pass = true;
    }

    vk_render_pass_encoder::~vk_render_pass_encoder()
    {
        if (m_in_pass)
        {
            end();
        }
    }

    void vk_render_pass_encoder::set_pipeline(pipeline pipeline_handle)
    {
        if (!m_in_pass)
        {
            return;
        }
        auto* pipe = m_device.lookup_pipeline(pipeline_handle);
        if (pipe == nullptr || pipe->is_compute || pipe->layout == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_render_pass_encoder::set_pipeline: bad pipeline (record=%p compute=%i layout=%p)",
                    static_cast<const void*>(pipe),
                    pipe != nullptr ? static_cast<int>(pipe->is_compute) : -1,
                    pipe != nullptr ? static_cast<const void*>(pipe->layout) : nullptr);
            return;
        }
        // Look up — or lazily build — the VkPipeline that is
        // compatible with the active render pass. The same
        // pipeline_descriptor maps to one VkPipeline per render
        // pass; the cache is owned by the vk_pipeline record.
        VkPipeline obj = m_device.graphics_pipeline_for(pipeline_handle, m_render_pass, m_y_flipped);
        if (obj == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_render_pass_encoder::set_pipeline: graphics_pipeline_for returned null");
            return;
        }
        m_pipeline_handle = pipeline_handle;
        m_current_pipeline_layout = pipe->layout;
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, obj);
    }

    void vk_render_pass_encoder::set_vertex_buffer(uint32_t slot,
                                                   buffer buffer_handle,
                                                   size_t offset,
                                                   uint32_t stride_override)
    {
        if (!m_in_pass)
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        VkBuffer obj = buf->object;
        VkDeviceSize off = offset;
        // The pipeline declares VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING
        // _STRIDE_EXT when the extension is available, in which case
        // the spec requires the stride to be supplied via
        // vkCmdBindVertexBuffers2EXT before any draw — calling the
        // non-2 variant leaves it undefined. If the caller passed a
        // stride_override (renderables that author pipelines with
        // stride==0 do this), use it; otherwise fall back to the
        // stride baked into the pipeline_descriptor we built it
        // from. tonemap_pass et al. take the latter branch.
        if (m_device.extended_dynamic_state_enabled())
        {
            VkDeviceSize stride = stride_override;
            if (stride == 0)
            {
                if (auto* pipe = m_device.lookup_pipeline(m_pipeline_handle))
                {
                    if (slot < pipe->descriptor.vertex_buffers.size())
                    {
                        stride = pipe->descriptor.vertex_buffers[slot].stride;
                    }
                }
            }
            m_device.cmd_bind_vertex_buffers2()(m_cmd, slot, 1, &obj, &off, nullptr, &stride);
            return;
        }
        vkCmdBindVertexBuffers(m_cmd, slot, 1, &obj, &off);
    }

    void vk_render_pass_encoder::set_index_buffer(buffer buffer_handle, index_format format)
    {
        if (!m_in_pass)
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdBindIndexBuffer(m_cmd, buf->object, 0, to_vk_index_type(format));
    }

    void vk_render_pass_encoder::set_bind_group(uint32_t group, bind_group bind_group_handle)
    {
        if (!m_in_pass || m_current_pipeline_layout == VK_NULL_HANDLE)
        {
            return;
        }
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (bg == nullptr || bg->descriptor_set == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdBindDescriptorSets(m_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_current_pipeline_layout,
                                group,
                                1,
                                &bg->descriptor_set,
                                0,
                                nullptr);
    }

    void vk_render_pass_encoder::set_viewport(int x, int y, int width, int height)
    {
        if (!m_in_pass)
        {
            return;
        }
        const VkViewport vp = make_viewport(static_cast<uint32_t>(x),
                                            static_cast<uint32_t>(y),
                                            static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height),
                                            m_y_flipped);
        vkCmdSetViewport(m_cmd, 0, 1, &vp);
        VkRect2D scissor{};
        scissor.offset = {x, y};
        scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    void vk_render_pass_encoder::draw(uint32_t vertex_count, uint32_t first_vertex)
    {
        if (m_in_pass)
        {
            vkCmdDraw(m_cmd, vertex_count, 1, first_vertex, 0);
            m_device.note_draw(vertex_count);
        }
    }

    void vk_render_pass_encoder::draw_indexed(uint32_t index_count, uint32_t first_index)
    {
        if (m_in_pass)
        {
            vkCmdDrawIndexed(m_cmd, index_count, 1, first_index, 0, 0);
            m_device.note_draw_indexed(index_count);
        }
    }

    void vk_render_pass_encoder::draw_indexed_indirect(buffer indirect_buffer, size_t offset)
    {
        if (!m_in_pass)
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(indirect_buffer);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdDrawIndexedIndirect(m_cmd, buf->object, offset, 1, 0);
    }

    void vk_render_pass_encoder::multi_draw_indexed_indirect(buffer indirect_buffer,
                                                             size_t offset,
                                                             uint32_t draw_count,
                                                             uint32_t stride)
    {
        if (!m_in_pass)
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(indirect_buffer);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdDrawIndexedIndirect(m_cmd, buf->object, offset, draw_count, stride);
    }

    void vk_render_pass_encoder::end()
    {
        if (!m_in_pass)
        {
            return;
        }
        vkCmdEndRenderPass(m_cmd);
        m_in_pass = false;
    }

    // -- vk_compute_pass_encoder -------------------------------------

    vk_compute_pass_encoder::vk_compute_pass_encoder(vk_device& device, VkCommandBuffer cmd)
        : m_device{device}, m_cmd{cmd}, m_active{cmd != VK_NULL_HANDLE}
    {
    }

    vk_compute_pass_encoder::~vk_compute_pass_encoder()
    {
        if (m_active)
        {
            end();
        }
    }

    void vk_compute_pass_encoder::set_pipeline(pipeline pipeline_handle)
    {
        if (!m_active)
        {
            return;
        }
        auto* pipe = m_device.lookup_pipeline(pipeline_handle);
        if (pipe == nullptr || !pipe->is_compute || pipe->compute_object == VK_NULL_HANDLE)
        {
            return;
        }
        m_current_pipeline_layout = pipe->layout;
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->compute_object);
    }

    void vk_compute_pass_encoder::set_bind_group(uint32_t group, bind_group bind_group_handle)
    {
        if (!m_active || m_current_pipeline_layout == VK_NULL_HANDLE)
        {
            return;
        }
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (bg == nullptr || bg->descriptor_set == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdBindDescriptorSets(m_cmd,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_current_pipeline_layout,
                                group,
                                1,
                                &bg->descriptor_set,
                                0,
                                nullptr);
    }

    void vk_compute_pass_encoder::dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        if (m_active)
        {
            vkCmdDispatch(m_cmd, x, y, z);
        }
    }

    void vk_compute_pass_encoder::end()
    {
        m_active = false;
    }

    // -- vk_command_encoder -----------------------------------------

    vk_command_encoder::vk_command_encoder(vk_device& device) : m_device{device}
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = device.command_pool();
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        const VkResult alloc_result = vkAllocateCommandBuffers(device.vk_handle(), &ai, &m_cmd);
        if (alloc_result != VK_SUCCESS)
        {
            LOG_ERR("vkAllocateCommandBuffers failed: %s", vk_result_to_string(alloc_result));
            m_cmd = VK_NULL_HANDLE;
            return;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        const VkResult begin_result = vkBeginCommandBuffer(m_cmd, &bi);
        if (begin_result != VK_SUCCESS)
        {
            LOG_ERR("vkBeginCommandBuffer failed: %s", vk_result_to_string(begin_result));
            vkFreeCommandBuffers(device.vk_handle(), device.command_pool(), 1, &m_cmd);
            m_cmd = VK_NULL_HANDLE;
            return;
        }
        m_began = true;
    }

    vk_command_encoder::~vk_command_encoder()
    {
        if (m_cmd != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(m_device.vk_handle(), m_device.command_pool(), 1, &m_cmd);
            m_cmd = VK_NULL_HANDLE;
        }
    }

    std::unique_ptr<render_pass_encoder> vk_command_encoder::begin_render_pass(const render_pass_descriptor& descriptor)
    {
        return std::make_unique<vk_render_pass_encoder>(m_device, m_cmd, descriptor);
    }

    std::unique_ptr<compute_pass_encoder> vk_command_encoder::begin_compute_pass()
    {
        return std::make_unique<vk_compute_pass_encoder>(m_device, m_cmd);
    }

    void
    vk_command_encoder::copy_buffer_to_buffer(buffer src, size_t src_offset, buffer dst, size_t dst_offset, size_t size)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* src_buf = m_device.lookup_buffer(src);
        auto* dst_buf = m_device.lookup_buffer(dst);
        if (src_buf == nullptr || dst_buf == nullptr || src_buf->object == VK_NULL_HANDLE ||
            dst_buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        VkBufferCopy region{};
        region.srcOffset = src_offset;
        region.dstOffset = dst_offset;
        region.size = size;
        vkCmdCopyBuffer(m_cmd, src_buf->object, dst_buf->object, 1, &region);
    }

    void vk_command_encoder::clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdFillBuffer(m_cmd, buf->object, offset, size, value);
    }

    void vk_command_encoder::barrier(pipeline_stage /*src_stage*/,
                                     pipeline_stage /*dst_stage*/,
                                     access_flag /*src_access*/,
                                     access_flag /*dst_access*/)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        // Conservative full barrier. The engine's existing usage
        // ranges are coarse enough that a stage-precise mapping is
        // not needed yet; revisit when compute / storage workloads
        // start churning per-frame.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(m_cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    VkCommandBuffer vk_command_encoder::release_command_buffer() noexcept
    {
        VkCommandBuffer released = m_cmd;
        m_cmd = VK_NULL_HANDLE;
        m_began = false;
        return released;
    }
} // namespace rendering_engine::gpu::backend::vulkan
