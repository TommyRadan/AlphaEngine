// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/backend/vulkan/vk_command_encoder.hpp>

#include <algorithm>
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
        //
        // @p x / @p y / @p width / @p height are the rectangle in the
        // window convention the abstract encoder uses (bottom-left
        // origin, as glViewport takes it). A flipped viewport covers
        // framebuffer rows [vp.y - height, vp.y] with NDC +Y at the
        // top row, so the bottom-left @p y maps to
        // vp.y = target_height - y: the rectangle sits where the GL
        // backend would put it, and a full-target rectangle is
        // unchanged (vp.y = target_height, height = -height).
        VkViewport
        make_viewport(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t target_height, bool y_flipped)
        {
            VkViewport vp{};
            vp.x = static_cast<float>(x);
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;
            if (y_flipped)
            {
                vp.y = static_cast<float>(static_cast<int32_t>(target_height) - y);
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

        // The scissor is a framebuffer-space rectangle that a negative
        // viewport height does not flip, so it is flipped alongside the
        // viewport: the bottom-left @p y becomes the top-left row
        // target_height - (y + height). Negative extents are clamped
        // away, since VkRect2D's extent is unsigned.
        VkRect2D
        make_scissor(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t target_height, bool y_flipped)
        {
            VkRect2D scissor{};
            scissor.offset.x = x;
            scissor.offset.y = y_flipped ? static_cast<int32_t>(target_height) - (y + height) : y;
            scissor.extent.width = static_cast<uint32_t>(std::max(width, 0));
            scissor.extent.height = static_cast<uint32_t>(std::max(height, 0));
            return scissor;
        }

        // A bind group that does not resolve — never created, already
        // destroyed, or its descriptor-set allocation failed — used to
        // be skipped silently, so the draw ran against whatever set was
        // bound before and produced wrong output with nothing in the
        // log. It is an error; reported once per handle per pass
        // encoder rather than once per draw.
        void report_missing_bind_group(std::vector<uint64_t>& reported,
                                       const char* encoder,
                                       uint32_t group,
                                       bind_group handle)
        {
            if (std::find(reported.begin(), reported.end(), handle.id) != reported.end())
            {
                return;
            }
            reported.push_back(handle.id);
            LOG_ERR("%s::set_bind_group: bind group %llu at slot %u is not a live descriptor set; "
                    "draws keep the previously bound set",
                    encoder,
                    static_cast<unsigned long long>(handle.id),
                    group);
        }

        // True when @p dynamic_offsets carries exactly one offset per
        // dynamic slot of @p bg: a mismatched count is invalid for
        // vkCmdBindDescriptorSets, so the group is not bound and the
        // call site is reported once per handle per pass encoder.
        bool dynamic_offsets_match(std::vector<uint64_t>& reported,
                                   const char* encoder,
                                   uint32_t group,
                                   bind_group handle,
                                   const vk_bind_group& bg,
                                   std::span<const uint32_t> dynamic_offsets)
        {
            if (dynamic_offsets.size() == bg.dynamic_count)
            {
                return true;
            }
            if (std::find(reported.begin(), reported.end(), handle.id) == reported.end())
            {
                reported.push_back(handle.id);
                LOG_ERR("%s::set_bind_group: bind group %llu at slot %u takes %u dynamic offsets, %zu given; "
                        "it is not bound",
                        encoder,
                        static_cast<unsigned long long>(handle.id),
                        group,
                        bg.dynamic_count,
                        dynamic_offsets.size());
            }
            return false;
        }

        // True when @p region lies inside @p record and @p size bytes
        // from @p offset stay inside @p buffer; logs the first problem
        // otherwise.
        bool copy_region_fits(const char* where,
                              const vk_texture& record,
                              const texture_copy_region& region,
                              const vk_buffer& buffer,
                              size_t offset)
        {
            if (record.samples > 1)
            {
                LOG_WRN("%s: a multisampled texture cannot be copied", where);
                return false;
            }
            if (region.mip_level >= record.mip_levels || region.layer >= record.array_layers)
            {
                LOG_WRN("%s: level %u / layer %u is outside the texture (%u levels, %u layers)",
                        where,
                        region.mip_level,
                        region.layer,
                        record.mip_levels,
                        record.array_layers);
                return false;
            }
            const uint32_t level_width = std::max(1u, record.width >> region.mip_level);
            const uint32_t level_height = std::max(1u, record.height >> region.mip_level);
            const uint32_t level_depth = std::max(1u, record.depth >> region.mip_level);
            if (region.width == 0 || region.height == 0 || region.depth == 0 ||
                static_cast<uint64_t>(region.x) + region.width > level_width ||
                static_cast<uint64_t>(region.y) + region.height > level_height ||
                static_cast<uint64_t>(region.z) + region.depth > level_depth)
            {
                LOG_WRN("%s: region does not fit level %u", where, region.mip_level);
                return false;
            }
            const size_t bytes = texture_region_bytes(record.format, region);
            if (offset > buffer.size || bytes > buffer.size - offset)
            {
                LOG_WRN("%s: %zu bytes at offset %zu exceed the %zu-byte buffer", where, bytes, offset, buffer.size);
                return false;
            }
            return true;
        }

        VkBufferImageCopy
        make_buffer_image_copy(const vk_texture& record, const texture_copy_region& region, size_t offset)
        {
            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource.aspectMask = (record.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u
                                                   ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                   : VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = region.mip_level;
            copy.imageSubresource.baseArrayLayer = region.layer;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {
                static_cast<int32_t>(region.x), static_cast<int32_t>(region.y), static_cast<int32_t>(region.z)};
            copy.imageExtent = {region.width, region.height, region.depth};
            return copy;
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

        if (target->is_swapchain)
        {
            // Acquire before reading anything off the target. The
            // frame's fence wait and deferred-destroy drain already ran
            // in vk_device::begin_frame; this acquires the image lazily,
            // so a frame that never reaches the swapchain does not
            // acquire one — and it may rebuild the swapchain on the
            // way, which retires every variant of this target (and the
            // framebuffers built on the old images), so a variant read
            // earlier would dangle. While the swapchain is suspended
            // (minimised) or the acquire failed, the pass records
            // nothing; the device has logged any real failure, and the
            // frame's other swapchain passes get the same answer.
            device.acquire_swapchain_image();
            if (!device.have_current_swapchain_image())
            {
                return;
            }
        }

        // The render pass is keyed by every attachment's load / store
        // op; the window backbuffer counts as one colour attachment.
        const uint32_t color_count = target->is_swapchain ? 1u : static_cast<uint32_t>(target->color.size());
        vk_render_pass_key key{};
        for (uint32_t i = 0; i < color_count && i < max_color_attachments; ++i)
        {
            key.color_load[i] = to_vk_load_op(descriptor.color[i].load);
            key.color_store[i] = to_vk_store_op(descriptor.color[i].store);
        }
        key.depth_load = to_vk_load_op(descriptor.depth.load);
        key.depth_store = to_vk_store_op(descriptor.depth.store);
        const bool use_depth = descriptor.use_depth && target->has_depth;
        key.use_depth = use_depth;
        VkRenderPass render_pass = device.acquire_render_pass(*target, key);
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
            // The acquired image paired with this frame slot's depth
            // buffer.
            const uint32_t idx = device.swapchain_framebuffer_index(device.current_swapchain_image_index());
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
        m_framebuffer = framebuffer;
        m_render_pass_generation = variant->render_pass_generation;
        m_color_count = variant->color_count;
        m_samples = to_vk_sample_count(target->samples);
        m_target_width = target->width;
        m_target_height = target->height;
        m_y_flipped = target->is_swapchain;
        m_secondary_contents = descriptor.parallel;
        device.note_render_pass_opened(target->is_swapchain, use_depth);

        // One clear value per attachment, in attachment order: the
        // colour attachments then depth (+ stencil). Values for
        // attachments that load or discard are ignored.
        std::array<VkClearValue, max_color_attachments + 1> clears{};
        uint32_t clear_count = 0;
        for (uint32_t i = 0; i < color_count && i < max_color_attachments; ++i)
        {
            clears[clear_count].color = {{descriptor.color[i].clear_color[0],
                                          descriptor.color[i].clear_color[1],
                                          descriptor.color[i].clear_color[2],
                                          descriptor.color[i].clear_color[3]}};
            ++clear_count;
        }
        if (use_depth)
        {
            clears[clear_count].depthStencil = {descriptor.depth.clear_depth, descriptor.depth.clear_stencil};
            ++clear_count;
        }

        VkRenderPassBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bi.renderPass = render_pass;
        bi.framebuffer = framebuffer;
        bi.renderArea.offset = {0, 0};
        bi.renderArea.extent = {target->width, target->height};
        bi.clearValueCount = clear_count;
        bi.pClearValues = clears.data();
        // A pass that takes its draws from secondaries records nothing
        // but vkCmdExecuteCommands until it ends — not even the dynamic
        // state, which every secondary sets for itself.
        vkCmdBeginRenderPass(m_cmd,
                             &bi,
                             m_secondary_contents ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS
                                                  : VK_SUBPASS_CONTENTS_INLINE);
        m_in_pass = true;
        if (m_secondary_contents)
        {
            return;
        }

        const VkViewport vp = make_viewport(0,
                                            0,
                                            static_cast<int32_t>(target->width),
                                            static_cast<int32_t>(target->height),
                                            target->height,
                                            m_y_flipped);
        vkCmdSetViewport(m_cmd, 0, 1, &vp);

        const VkRect2D scissor = make_scissor(0,
                                              0,
                                              static_cast<int32_t>(target->width),
                                              static_cast<int32_t>(target->height),
                                              target->height,
                                              m_y_flipped);
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    vk_render_pass_encoder::vk_render_pass_encoder(vk_device& device,
                                                   VkCommandBuffer cmd,
                                                   const vk_render_pass_encoder& primary)
        : m_device{device}, m_cmd{cmd}
    {
        m_render_pass = primary.m_render_pass;
        m_framebuffer = primary.m_framebuffer;
        m_render_pass_generation = primary.m_render_pass_generation;
        m_color_count = primary.m_color_count;
        m_samples = primary.m_samples;
        m_target_width = primary.m_target_width;
        m_target_height = primary.m_target_height;
        m_y_flipped = primary.m_y_flipped;
        m_secondary = true;
        if (cmd == VK_NULL_HANDLE)
        {
            return;
        }

        // The secondary continues the primary's render pass instance:
        // it inherits the pass and framebuffer (so its pipelines match
        // the pass's attachments, like a variant bound on the primary
        // would) and is executed exactly once by the primary.
        VkCommandBufferInheritanceInfo inheritance{};
        inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritance.renderPass = m_render_pass;
        inheritance.subpass = 0;
        inheritance.framebuffer = m_framebuffer;
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        begin.pInheritanceInfo = &inheritance;
        if (!vk_check(vkBeginCommandBuffer(m_cmd, &begin), "vkBeginCommandBuffer (secondary)"))
        {
            m_cmd = VK_NULL_HANDLE;
            return;
        }
        m_in_pass = true;

        // Dynamic state does not carry over from the primary: the
        // viewport and scissor start at the target's extent, as in a
        // freshly begun pass.
        const VkViewport vp = make_viewport(0,
                                            0,
                                            static_cast<int32_t>(m_target_width),
                                            static_cast<int32_t>(m_target_height),
                                            m_target_height,
                                            m_y_flipped);
        vkCmdSetViewport(m_cmd, 0, 1, &vp);
        const VkRect2D scissor = make_scissor(0,
                                              0,
                                              static_cast<int32_t>(m_target_width),
                                              static_cast<int32_t>(m_target_height),
                                              m_target_height,
                                              m_y_flipped);
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    vk_render_pass_encoder::~vk_render_pass_encoder()
    {
        if (m_in_pass)
        {
            end();
        }
    }

    bool vk_render_pass_encoder::inline_recording()
    {
        if (!m_in_pass)
        {
            return false;
        }
        if (!m_secondary_contents)
        {
            return true;
        }
        if (!m_inline_reported)
        {
            m_inline_reported = true;
            LOG_ERR("vk_render_pass_encoder: a pass begun with render_pass_descriptor::parallel takes its draws from "
                    "secondary encoders; a draw-level call recorded on the primary is dropped");
        }
        return false;
    }

    void vk_render_pass_encoder::tally_draw(uint32_t vertex_count)
    {
        if (m_secondary)
        {
            ++m_tally.draws;
            m_tally.vertices += vertex_count;
            return;
        }
        m_device.note_draw(vertex_count);
    }

    void vk_render_pass_encoder::tally_draw_indexed(uint32_t index_count)
    {
        if (m_secondary)
        {
            ++m_tally.draws_indexed;
            m_tally.indices += index_count;
            return;
        }
        m_device.note_draw_indexed(index_count);
    }

    std::unique_ptr<render_pass_encoder> vk_render_pass_encoder::begin_secondary(uint32_t lane)
    {
        if (!m_in_pass || m_secondary)
        {
            LOG_ERR("vk_render_pass_encoder::begin_secondary: %s",
                    m_secondary ? "a secondary cannot open secondaries of its own" : "the pass is not open");
            return nullptr;
        }
        if (!m_secondary_contents)
        {
            LOG_ERR("vk_render_pass_encoder::begin_secondary: the pass was not begun with "
                    "render_pass_descriptor::parallel");
            return nullptr;
        }
        // Main thread, before the fork: the lane's pool is handed to the
        // thread that records the secondary from here until the join.
        VkCommandBuffer cmd = m_device.acquire_secondary_command_buffer(lane);
        if (cmd == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_render_pass_encoder::begin_secondary: no secondary command buffer for lane %u", lane);
            return nullptr;
        }
        auto secondary = std::make_unique<vk_render_pass_encoder>(m_device, cmd, *this);
        if (!secondary->m_in_pass)
        {
            // vkBeginCommandBuffer failed and was logged; the buffer goes
            // back with the pool reset.
            return nullptr;
        }
        return secondary;
    }

    void vk_render_pass_encoder::execute_secondary(render_pass_encoder& secondary)
    {
        if (!m_in_pass || m_secondary || !m_secondary_contents)
        {
            LOG_ERR("vk_render_pass_encoder::execute_secondary: not an open pass that takes secondaries");
            return;
        }
        // Only this backend makes secondaries, through begin_secondary
        // on a pass like this one.
        auto& other = static_cast<vk_render_pass_encoder&>(secondary);
        if (!other.m_secondary || other.m_render_pass != m_render_pass || other.m_framebuffer != m_framebuffer)
        {
            LOG_ERR("vk_render_pass_encoder::execute_secondary: the encoder is not a secondary of this pass");
            return;
        }
        if (!other.m_ended || other.m_executed)
        {
            LOG_ERR("vk_render_pass_encoder::execute_secondary: the secondary %s",
                    other.m_executed ? "was already executed" : "has not ended");
            return;
        }
        vkCmdExecuteCommands(m_cmd, 1, &other.m_cmd);
        other.m_executed = true;
        // Its draws count toward this frame like inline ones.
        m_device.note_draws(
            other.m_tally.draws, other.m_tally.vertices, other.m_tally.draws_indexed, other.m_tally.indices);
        other.m_tally = {};
    }

    void vk_render_pass_encoder::set_pipeline(pipeline pipeline_handle)
    {
        if (!inline_recording())
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
        VkPipeline obj = m_device.graphics_pipeline_for(
            pipeline_handle, m_render_pass, m_render_pass_generation, m_y_flipped, m_color_count, m_samples);
        if (obj == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_render_pass_encoder::set_pipeline: graphics_pipeline_for returned null");
            return;
        }
        m_pipeline_handle = pipeline_handle;
        m_current_pipeline_layout = pipe->layout;
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, obj);
        // Every graphics pipeline declares the stencil reference
        // dynamic, so it is supplied after each bind.
        vkCmdSetStencilReference(m_cmd, VK_STENCIL_FACE_FRONT_AND_BACK, m_stencil_reference);
    }

    void vk_render_pass_encoder::set_vertex_buffer(uint32_t slot,
                                                   buffer buffer_handle,
                                                   size_t offset,
                                                   uint32_t stride_override)
    {
        if (!inline_recording())
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        // A multi-buffered buffer (an instance stream, the sprite
        // batch's quads) is read through this frame slot's copy, brought
        // up to date first.
        m_device.ensure_host_region_current(*buf);
        VkBuffer obj = buf->object;
        VkDeviceSize off = m_device.host_region_offset(*buf) + offset;
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
        if (!inline_recording())
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        m_device.ensure_host_region_current(*buf);
        vkCmdBindIndexBuffer(m_cmd, buf->object, m_device.host_region_offset(*buf), to_vk_index_type(format));
    }

    void vk_render_pass_encoder::set_bind_group(uint32_t group,
                                                bind_group bind_group_handle,
                                                std::span<const uint32_t> dynamic_offsets)
    {
        if (!inline_recording() || m_current_pipeline_layout == VK_NULL_HANDLE)
        {
            return;
        }
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (bg == nullptr || bg->set_count == 0)
        {
            report_missing_bind_group(m_reported_bind_groups, "vk_render_pass_encoder", group, bind_group_handle);
            return;
        }
        if (!dynamic_offsets_match(
                m_reported_bind_groups, "vk_render_pass_encoder", group, bind_group_handle, *bg, dynamic_offsets))
        {
            return;
        }
        // This frame slot's set: it reads the slot's copy of every
        // multi-buffered buffer, each brought up to date first.
        m_device.prepare_bind_group(*bg);
        const VkDescriptorSet set = bg->descriptor_set(m_device.frame_slot());
        vkCmdBindDescriptorSets(m_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_current_pipeline_layout,
                                group,
                                1,
                                &set,
                                static_cast<uint32_t>(dynamic_offsets.size()),
                                dynamic_offsets.data());
    }

    void vk_render_pass_encoder::push_constants(shader_stages stages, uint32_t offset, uint32_t size, const void* data)
    {
        if (!inline_recording() || m_current_pipeline_layout == VK_NULL_HANDLE || data == nullptr)
        {
            return;
        }
        // The pushed bytes must sit inside a range the pipeline layout
        // declares for exactly these stages, and every declared range
        // they overlap must name the same stages (VUID-vkCmdPushConstants
        // -offset-01795 / -01796).
        const auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        const uint64_t end = static_cast<uint64_t>(offset) + size;
        bool covered = false;
        bool conflicting = false;
        if (pipe != nullptr)
        {
            for (const auto& range : pipe->descriptor.push_constant_ranges)
            {
                const uint64_t range_end = static_cast<uint64_t>(range.offset) + range.size;
                if (offset >= range_end || end <= range.offset)
                {
                    continue;
                }
                if (range.stages != stages)
                {
                    conflicting = true;
                }
                else if (offset >= range.offset && end <= range_end)
                {
                    covered = true;
                }
            }
        }
        if (size == 0 || offset % 4u != 0u || size % 4u != 0u || stages == 0u || !covered || conflicting)
        {
            if (!m_push_constants_reported)
            {
                m_push_constants_reported = true;
                LOG_ERR("vk_render_pass_encoder::push_constants: %u bytes at offset %u for stages 0x%x do not match a "
                        "push-constant range of pipeline %llu; the push is dropped",
                        size,
                        offset,
                        stages,
                        static_cast<unsigned long long>(m_pipeline_handle.id));
            }
            return;
        }
        vkCmdPushConstants(m_cmd, m_current_pipeline_layout, to_vk_stage_flags(stages), offset, size, data);
    }

    void vk_render_pass_encoder::set_viewport(int x, int y, int width, int height)
    {
        if (!inline_recording())
        {
            return;
        }
        const VkViewport vp = make_viewport(x, y, width, height, m_target_height, m_y_flipped);
        vkCmdSetViewport(m_cmd, 0, 1, &vp);
        const VkRect2D scissor = make_scissor(x, y, width, height, m_target_height, m_y_flipped);
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    void vk_render_pass_encoder::set_scissor(int x, int y, int width, int height)
    {
        if (!inline_recording())
        {
            return;
        }
        const VkRect2D scissor = make_scissor(x, y, width, height, m_target_height, m_y_flipped);
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    void vk_render_pass_encoder::set_stencil_reference(uint32_t reference)
    {
        m_stencil_reference = reference;
        if (inline_recording())
        {
            vkCmdSetStencilReference(m_cmd, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
        }
    }

    void vk_render_pass_encoder::draw(uint32_t vertex_count,
                                      uint32_t instance_count,
                                      uint32_t first_vertex,
                                      uint32_t first_instance)
    {
        if (inline_recording())
        {
            vkCmdDraw(m_cmd, vertex_count, instance_count, first_vertex, first_instance);
            tally_draw(vertex_count);
        }
    }

    void vk_render_pass_encoder::draw_indexed(uint32_t index_count,
                                              uint32_t instance_count,
                                              uint32_t first_index,
                                              int32_t base_vertex,
                                              uint32_t first_instance)
    {
        if (inline_recording())
        {
            vkCmdDrawIndexed(m_cmd, index_count, instance_count, first_index, base_vertex, first_instance);
            tally_draw_indexed(index_count);
        }
    }

    void vk_render_pass_encoder::draw_indexed_indirect(buffer indirect_buffer, size_t offset)
    {
        if (!inline_recording())
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(indirect_buffer);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        m_device.ensure_host_region_current(*buf);
        vkCmdDrawIndexedIndirect(m_cmd, buf->object, m_device.host_region_offset(*buf) + offset, 1, 0);
    }

    void vk_render_pass_encoder::multi_draw_indexed_indirect(buffer indirect_buffer,
                                                             size_t offset,
                                                             uint32_t draw_count,
                                                             uint32_t stride)
    {
        if (!inline_recording())
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(indirect_buffer);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        m_device.ensure_host_region_current(*buf);
        const VkDeviceSize base = m_device.host_region_offset(*buf) + offset;
        if (draw_count > 1 && !m_device.features().multi_draw_indirect)
        {
            // Without multiDrawIndirect a drawCount above one is
            // illegal; the records are issued one at a time, stepping
            // by the caller's stride.
            for (uint32_t i = 0; i < draw_count; ++i)
            {
                vkCmdDrawIndexedIndirect(m_cmd, buf->object, base + static_cast<VkDeviceSize>(i) * stride, 1, stride);
            }
            return;
        }
        vkCmdDrawIndexedIndirect(m_cmd, buf->object, base, draw_count, stride);
    }

    void vk_render_pass_encoder::end()
    {
        if (!m_in_pass)
        {
            return;
        }
        m_in_pass = false;
        if (m_secondary)
        {
            // On the thread that recorded it; the primary executes it
            // afterwards. A buffer that failed to end is never executed.
            if (vk_check(vkEndCommandBuffer(m_cmd), "vkEndCommandBuffer (secondary)"))
            {
                m_ended = true;
            }
            return;
        }
        vkCmdEndRenderPass(m_cmd);
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

    void vk_compute_pass_encoder::set_bind_group(uint32_t group,
                                                 bind_group bind_group_handle,
                                                 std::span<const uint32_t> dynamic_offsets)
    {
        if (!m_active || m_current_pipeline_layout == VK_NULL_HANDLE)
        {
            return;
        }
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (bg == nullptr || bg->set_count == 0)
        {
            report_missing_bind_group(m_reported_bind_groups, "vk_compute_pass_encoder", group, bind_group_handle);
            return;
        }
        if (!dynamic_offsets_match(
                m_reported_bind_groups, "vk_compute_pass_encoder", group, bind_group_handle, *bg, dynamic_offsets))
        {
            return;
        }
        // The descriptors declare storage images as VK_IMAGE_LAYOUT_GENERAL,
        // so move any bound storage texture there before the dispatch reads
        // the set. transition_storage_image returns true only on the first
        // move, so a texture bound across several mip dispatches is tracked
        // (and later restored) exactly once.
        for (const auto& entry : bg->entries)
        {
            if (entry.kind == binding_kind::storage_texture &&
                m_device.transition_storage_image(m_cmd, entry.texture_value, true))
            {
                m_storage_textures.push_back(entry.texture_value);
            }
        }
        // This frame slot's set; see vk_render_pass_encoder::set_bind_group.
        m_device.prepare_bind_group(*bg);
        const VkDescriptorSet set = bg->descriptor_set(m_device.frame_slot());
        vkCmdBindDescriptorSets(m_cmd,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_current_pipeline_layout,
                                group,
                                1,
                                &set,
                                static_cast<uint32_t>(dynamic_offsets.size()),
                                dynamic_offsets.data());
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
        // Hand the freshly-written storage images back to the sampled
        // layout so the lighting passes that read them (irradiance,
        // prefiltered, BRDF LUT) see a SHADER_READ_ONLY_OPTIMAL image.
        for (texture handle : m_storage_textures)
        {
            m_device.transition_storage_image(m_cmd, handle, false);
        }
        m_storage_textures.clear();
        m_active = false;
    }

    // -- vk_command_encoder -----------------------------------------

    vk_command_encoder::vk_command_encoder(vk_device& device) : m_device{device}
    {
        // The buffer comes from the device's frame command pool, which
        // hands it out reset and takes it back with the pool reset at
        // the next begin_frame: nothing is allocated or freed per frame
        // once the pool has grown to the frame's encoder count. A null
        // buffer (lost device, failed allocation — logged there) leaves
        // the encoder inert: every pass it begins records nothing.
        m_cmd = device.acquire_frame_command_buffer();
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        const VkResult begin_result = vkBeginCommandBuffer(m_cmd, &bi);
        if (begin_result != VK_SUCCESS)
        {
            LOG_ERR("vkBeginCommandBuffer failed: %s", vk_result_to_string(begin_result));
            m_cmd = VK_NULL_HANDLE;
            return;
        }
        m_began = true;
    }

    vk_command_encoder::~vk_command_encoder()
    {
        // An encoder dropped without a submit leaves its buffer to the
        // pool: the reset at the next begin_frame reclaims it along with
        // the frame's submitted one.
        m_cmd = VK_NULL_HANDLE;
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
        // Both sides go through this frame slot's copy. A GPU write
        // into a multi-buffered destination reaches that copy alone:
        // the device carries host writes across the copies, not what
        // the GPU wrote (see vk_buffer::region_count).
        m_device.ensure_host_region_current(*src_buf);
        VkBufferCopy region{};
        region.srcOffset = m_device.host_region_offset(*src_buf) + src_offset;
        region.dstOffset = m_device.host_region_offset(*dst_buf) + dst_offset;
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
        // See copy_buffer_to_buffer on a multi-buffered destination.
        vkCmdFillBuffer(m_cmd, buf->object, m_device.host_region_offset(*buf) + offset, size, value);
    }

    void vk_command_encoder::barrier(pipeline_stage src_stage,
                                     pipeline_stage dst_stage,
                                     access_flag src_access,
                                     access_flag dst_access)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        // Translate the caller's stage/access intent into precise Vulkan masks
        // instead of a blanket ALL_COMMANDS full barrier. An empty stage mask
        // is illegal, so an unspecified source waits from the top of the pipe
        // and an unspecified destination blocks at the bottom.
        VkPipelineStageFlags src = to_vk_pipeline_stage(src_stage);
        VkPipelineStageFlags dst = to_vk_pipeline_stage(dst_stage);
        if (src == 0u)
        {
            src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
        if (dst == 0u)
        {
            dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = to_vk_access(src_access);
        mb.dstAccessMask = to_vk_access(dst_access);
        vkCmdPipelineBarrier(m_cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    void vk_command_encoder::copy_buffer_to_texture(buffer src,
                                                    size_t src_offset,
                                                    texture dst,
                                                    const texture_copy_region& region)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* src_buf = m_device.lookup_buffer(src);
        auto* dst_tex = m_device.lookup_texture(dst);
        if (src_buf == nullptr || src_buf->object == VK_NULL_HANDLE || dst_tex == nullptr ||
            dst_tex->image == VK_NULL_HANDLE)
        {
            LOG_WRN("copy_buffer_to_texture: invalid src buffer / dst texture");
            return;
        }
        if ((dst_tex->usage & texture_usage_copy_dst) == 0u)
        {
            LOG_WRN("copy_buffer_to_texture: the texture was created without texture_usage_copy_dst");
            return;
        }
        if (!copy_region_fits("copy_buffer_to_texture", *dst_tex, region, *src_buf, src_offset))
        {
            return;
        }
        // The buffer's writes (a host write, or an earlier copy in this
        // stream) must be visible to the transfer; the image moves to
        // the transfer layout for the copy and back to its resting
        // layout, in stream order with the passes around it.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(m_cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
        const VkImageLayout rest = dst_tex->layout;
        m_device.record_layout_transition(m_cmd, *dst_tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        m_device.ensure_host_region_current(*src_buf);
        const VkBufferImageCopy copy =
            make_buffer_image_copy(*dst_tex, region, m_device.host_region_offset(*src_buf) + src_offset);
        vkCmdCopyBufferToImage(m_cmd, src_buf->object, dst_tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        m_device.record_layout_transition(
            m_cmd, *dst_tex, rest == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : rest);
    }

    void vk_command_encoder::copy_texture_to_buffer(texture src,
                                                    const texture_copy_region& region,
                                                    buffer dst,
                                                    size_t dst_offset)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* src_tex = m_device.lookup_texture(src);
        auto* dst_buf = m_device.lookup_buffer(dst);
        if (src_tex == nullptr || src_tex->image == VK_NULL_HANDLE || dst_buf == nullptr ||
            dst_buf->object == VK_NULL_HANDLE)
        {
            LOG_WRN("copy_texture_to_buffer: invalid src texture / dst buffer");
            return;
        }
        if ((src_tex->usage & texture_usage_copy_src) == 0u)
        {
            LOG_WRN("copy_texture_to_buffer: the texture was created without texture_usage_copy_src");
            return;
        }
        if (!copy_region_fits("copy_texture_to_buffer", *src_tex, region, *dst_buf, dst_offset))
        {
            return;
        }
        const VkImageLayout rest = src_tex->layout;
        m_device.record_layout_transition(m_cmd, *src_tex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        // See copy_buffer_to_buffer on a multi-buffered destination.
        const VkBufferImageCopy copy =
            make_buffer_image_copy(*src_tex, region, m_device.host_region_offset(*dst_buf) + dst_offset);
        vkCmdCopyImageToBuffer(m_cmd, src_tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_buf->object, 1, &copy);
        m_device.record_layout_transition(
            m_cmd, *src_tex, rest == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : rest);
        // Make the transferred bytes visible to a host read (a mapped
        // readback buffer) and to whatever consumes the buffer next.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(m_cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    void vk_command_encoder::push_debug_group(const char* name)
    {
        const PFN_vkCmdBeginDebugUtilsLabelEXT begin_label = m_device.cmd_begin_debug_label();
        if (m_cmd == VK_NULL_HANDLE || begin_label == nullptr)
        {
            return;
        }
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name != nullptr ? name : "";
        begin_label(m_cmd, &label);
        ++m_debug_label_depth;
    }

    void vk_command_encoder::pop_debug_group()
    {
        const PFN_vkCmdEndDebugUtilsLabelEXT end_label = m_device.cmd_end_debug_label();
        if (m_cmd == VK_NULL_HANDLE || end_label == nullptr || m_debug_label_depth == 0)
        {
            return;
        }
        end_label(m_cmd);
        --m_debug_label_depth;
    }

    void vk_command_encoder::reset_queries(query_set set, uint32_t first, uint32_t count)
    {
        if (m_cmd == VK_NULL_HANDLE || count == 0)
        {
            return;
        }
        auto* record = m_device.lookup_query_set(set);
        if (record == nullptr || record->pool == VK_NULL_HANDLE)
        {
            return;
        }
        if (first > record->count || count > record->count - first)
        {
            LOG_WRN("reset_queries: %u queries from %u exceed the %u-query set", count, first, record->count);
            return;
        }
        // Must be recorded outside a render pass; the caller resets a
        // frame's queries at its top, before any pass opens.
        vkCmdResetQueryPool(m_cmd, record->pool, first, count);
    }

    void vk_command_encoder::write_timestamp(query_set set, uint32_t index)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* record = m_device.lookup_query_set(set);
        if (record == nullptr || record->pool == VK_NULL_HANDLE)
        {
            return;
        }
        if (index >= record->count)
        {
            LOG_WRN("write_timestamp: query %u of a %u-query set", index, record->count);
            return;
        }
        // Bottom of pipe: the stamp lands once everything recorded
        // before it has executed, which is what a per-pass interval
        // wants.
        vkCmdWriteTimestamp(m_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, record->pool, index);
    }

    VkCommandBuffer vk_command_encoder::release_command_buffer() noexcept
    {
        VkCommandBuffer released = m_cmd;
        m_cmd = VK_NULL_HANDLE;
        m_began = false;
        return released;
    }
} // namespace rendering_engine::gpu::backend::vulkan
