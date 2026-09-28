// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_render_pass_cache.cpp
 * @brief @c vk_render_pass_cache: render-pass variants, their
 *        framebuffers, and their retirement.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_render_pass_cache.hpp>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_frame.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_physical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_swapchain.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    vk_render_pass_cache::vk_render_pass_cache(const vk_physical_device& physical_device,
                                               vk_logical_device& device,
                                               vk_frame& frame,
                                               const vk_swapchain& swapchain,
                                               handle_pool<vk_texture>& textures,
                                               handle_pool<vk_pipeline>& pipelines)
        : m_physical_device{physical_device}, m_device{device}, m_frame{frame}, m_swapchain{swapchain},
          m_textures{textures}, m_pipelines{pipelines}
    {
    }

    VkRenderPass vk_render_pass_cache::acquire_render_pass(vk_render_target& target,
                                                           VkAttachmentLoadOp color_load,
                                                           VkAttachmentLoadOp depth_load,
                                                           bool use_depth)
    {
        vk_render_pass_key key{};
        key.color_load.fill(color_load);
        key.color_store.fill(VK_ATTACHMENT_STORE_OP_STORE);
        key.depth_load = depth_load;
        key.depth_store = VK_ATTACHMENT_STORE_OP_STORE;
        key.use_depth = use_depth;
        return acquire_render_pass(target, key);
    }

    VkRenderPass vk_render_pass_cache::acquire_render_pass(vk_render_target& target,
                                                           const vk_render_pass_key& requested)
    {
        // The window backbuffer counts as one colour attachment. Key
        // entries past the attachment count are irrelevant, so they
        // are normalised away: two callers that agree on the used
        // attachments share the variant whatever they left in the rest.
        const uint32_t color_count = target.is_swapchain ? 1u : static_cast<uint32_t>(target.color.size());
        const bool variant_uses_depth = requested.use_depth && target.has_depth;
        vk_render_pass_key key = requested;
        key.use_depth = variant_uses_depth;
        for (uint32_t i = color_count; i < max_color_attachments; ++i)
        {
            key.color_load[i] = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            key.color_store[i] = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        if (!variant_uses_depth)
        {
            key.depth_load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            key.depth_store = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }

        // Reuse a matching variant if one already exists.
        for (const auto& v : target.variants)
        {
            if (v.key == key && v.render_pass != VK_NULL_HANDLE)
            {
                return v.render_pass;
            }
        }

        const VkSampleCountFlagBits samples = to_vk_sample_count(target.samples);

        std::array<VkAttachmentDescription, max_color_attachments + 1> attachments{};
        std::array<VkAttachmentReference, max_color_attachments> color_refs{};
        VkAttachmentReference depth_ref{};
        uint32_t attachment_count = 0;

        for (uint32_t i = 0; i < color_count; ++i)
        {
            VkAttachmentDescription& attachment = attachments[attachment_count];
            attachment = {};
            if (target.is_swapchain)
            {
                attachment.format = m_swapchain.surface_format();
            }
            else if (auto* color_tex = m_textures.lookup(target.color[i].tex.id))
            {
                attachment.format = color_tex->vk_format;
            }
            else
            {
                attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
            }
            attachment.samples = samples;
            attachment.loadOp = key.color_load[i];
            attachment.storeOp = key.color_store[i];
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout =
                key.color_load[i] == VK_ATTACHMENT_LOAD_OP_LOAD
                    ? (target.is_swapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    : VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout =
                target.is_swapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            color_refs[i].attachment = attachment_count;
            color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ++attachment_count;
        }

        if (variant_uses_depth)
        {
            VkAttachmentDescription& attachment = attachments[attachment_count];
            attachment = {};
            if (target.is_swapchain)
            {
                attachment.format = m_physical_device.vk_format_for(m_swapchain.depth_format());
            }
            else if (auto* depth_tex = m_textures.lookup(target.depth.tex.id))
            {
                attachment.format = depth_tex->vk_format;
            }
            else
            {
                attachment.format = VK_FORMAT_D32_SFLOAT;
            }
            // Off-screen depth is sampled by later post passes (the velocity
            // pass reads sceneDepth, the lit materials the shadow maps), so
            // it leaves the pass in the shader-read layout the
            // combined-image-sampler descriptor expects, mirroring the
            // off-screen colour attachment above. A LOAD therefore resumes
            // from that same layout. The swapchain depth is never sampled,
            // so it stays in the attachment layout. A stencil plane, when
            // the format has one, loads and stores with depth.
            const VkImageLayout depth_rest_layout = target.is_swapchain
                                                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachment.samples = samples;
            attachment.loadOp = key.depth_load;
            attachment.storeOp = key.depth_store;
            attachment.stencilLoadOp = target.has_stencil ? key.depth_load : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = target.has_stencil ? key.depth_store : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout =
                key.depth_load == VK_ATTACHMENT_LOAD_OP_LOAD ? depth_rest_layout : VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = depth_rest_layout;
            depth_ref.attachment = attachment_count;
            depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachment_count;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = color_count;
        subpass.pColorAttachments = color_refs.data();
        subpass.pDepthStencilAttachment = variant_uses_depth ? &depth_ref : nullptr;

        // Two subpass dependencies. The EXTERNAL → 0 incoming dep
        // synchronises this pass's writes, and any LOAD it performs,
        // against a previous render pass's color/depth writes to the
        // same attachment — needed when consecutive passes target the
        // same off-screen or swapchain image. It covers both write
        // stages (late fragment tests for depth, since a depth write
        // only completes there) and both directions a LOAD can hazard
        // against a prior write: the automatic layout transition on
        // entry is itself a read of the old contents, and blending
        // reads the loaded color. Without this, syncval reports
        // READ_AFTER_WRITE against the layout transition whenever a
        // pass loads an attachment a previous pass wrote (skybox and
        // bloom's composite loading the HDR target, the UI and debug
        // passes loading the swapchain image). The 0 → EXTERNAL
        // outgoing dep releases color writes from this pass for
        // FRAGMENT_SHADER reads in a subsequent pass — without it
        // tonemap's fragment shader can sample the scene HDR target
        // before scene_pass's color writes are visible, and the
        // sample silently returns undefined data (typically zeros).
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstStageMask = deps[0].srcStageMask;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        // Release both colour and depth writes for a subsequent pass's
        // fragment-shader reads: tonemap samples the HDR colour and the
        // velocity pass samples the scene depth, so the depth write
        // (completed at late fragment tests) must be made visible too.
        deps[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = attachment_count;
        rpi.pAttachments = attachments.data();
        rpi.subpassCount = 1;
        rpi.pSubpasses = &subpass;
        rpi.dependencyCount = static_cast<uint32_t>(deps.size());
        rpi.pDependencies = deps.data();

        VkRenderPass new_render_pass = VK_NULL_HANDLE;
        const VkResult rp_result = vkCreateRenderPass(m_device.handle(), &rpi, nullptr, &new_render_pass);
        if (rp_result != VK_SUCCESS)
        {
            LOG_ERR("vkCreateRenderPass failed: %s (colour attachments=%u color_load[0]=%i depth_load=%i use_depth=%i)",
                    vk_result_to_string(rp_result),
                    color_count,
                    static_cast<int>(key.color_load[0]),
                    static_cast<int>(key.depth_load),
                    static_cast<int>(variant_uses_depth));
            return VK_NULL_HANDLE;
        }
        vk_render_target::variant new_variant{};
        new_variant.key = key;
        new_variant.render_pass = new_render_pass;
        new_variant.color_count = color_count;
        // The generation is what the pipeline cache keys on; it is
        // never reused, unlike the handle value the driver hands out.
        new_variant.render_pass_generation = m_next_render_pass_generation++;
        target.variants.push_back(std::move(new_variant));
        auto& v = target.variants.back();

        // Build per-variant framebuffers. Variants with different
        // use_depth aren't render-pass compatible (different
        // attachment counts), so they need their own framebuffer
        // sets — sharing one framebuffer across them would fail
        // vkCmdBeginRenderPass with INVALID_RENDER_PASS.
        if (target.is_swapchain)
        {
            // One framebuffer per (swapchain image, frame slot) pair,
            // since the depth attachment is the slot's; the encoder
            // picks by swapchain_framebuffer_index.
            const size_t image_count = m_swapchain.image_views().size();
            v.framebuffers.resize(image_count * m_frame.frames_in_flight(), VK_NULL_HANDLE);
            for (size_t i = 0; i < image_count; ++i)
            {
                for (uint32_t slot = 0; slot < m_frame.frames_in_flight(); ++slot)
                {
                    std::array<VkImageView, 2> views{m_swapchain.image_views()[i], m_swapchain.depth_view(slot)};
                    VkFramebufferCreateInfo fbi{};
                    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    fbi.renderPass = new_render_pass;
                    fbi.attachmentCount = variant_uses_depth ? 2 : 1;
                    fbi.pAttachments = views.data();
                    fbi.width = m_swapchain.extent().width;
                    fbi.height = m_swapchain.extent().height;
                    fbi.layers = 1;
                    VkFramebuffer& framebuffer = v.framebuffers[i * m_frame.frames_in_flight() + slot];
                    const VkResult fb_result = vkCreateFramebuffer(m_device.handle(), &fbi, nullptr, &framebuffer);
                    if (fb_result != VK_SUCCESS)
                    {
                        LOG_ERR("vkCreateFramebuffer (swapchain image %u, slot %u) failed: %s",
                                static_cast<unsigned>(i),
                                slot,
                                vk_result_to_string(fb_result));
                    }
                }
            }
        }
        else
        {
            // Every attachment renders through a single-level,
            // single-layer view of its texture: the mip and the layer /
            // cube face the target attached.
            v.framebuffers.resize(1, VK_NULL_HANDLE);
            std::array<VkImageView, max_color_attachments + 1> views{};
            uint32_t view_count = 0;
            bool views_ok = true;
            const auto add_view = [&](const vk_attachment& attachment)
            {
                VkImageView view = VK_NULL_HANDLE;
                if (auto* tex = m_textures.lookup(attachment.tex.id))
                {
                    view = attachment_image_view(*tex, attachment.mip_level, attachment.layer);
                }
                if (view == VK_NULL_HANDLE)
                {
                    views_ok = false;
                }
                views[view_count++] = view;
            };
            for (uint32_t i = 0; i < color_count; ++i)
            {
                add_view(target.color[i]);
            }
            if (variant_uses_depth)
            {
                add_view(target.depth);
            }
            if (!views_ok)
            {
                LOG_ERR("acquire_render_pass: an attachment of the off-screen target has no image view");
            }
            else
            {
                VkFramebufferCreateInfo fbi{};
                fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbi.renderPass = new_render_pass;
                fbi.attachmentCount = view_count;
                fbi.pAttachments = views.data();
                fbi.width = target.width;
                fbi.height = target.height;
                fbi.layers = 1;
                const VkResult fb_result = vkCreateFramebuffer(m_device.handle(), &fbi, nullptr, &v.framebuffers[0]);
                if (fb_result != VK_SUCCESS)
                {
                    LOG_ERR("vkCreateFramebuffer (offscreen) failed: %s", vk_result_to_string(fb_result));
                }
            }
        }

        return new_render_pass;
    }

    VkImageView vk_render_pass_cache::attachment_image_view(vk_texture& tex, uint32_t mip, uint32_t layer)
    {
        if (tex.image == VK_NULL_HANDLE || mip >= tex.mip_levels || layer >= tex.array_layers)
        {
            return VK_NULL_HANDLE;
        }
        const size_t index = static_cast<size_t>(layer) * tex.mip_levels + mip;
        if (index >= tex.attachment_views.size())
        {
            return VK_NULL_HANDLE;
        }
        if (tex.attachment_views[index] != VK_NULL_HANDLE)
        {
            return tex.attachment_views[index];
        }

        // One level, one layer, as a plain 2D view whatever the image's
        // own shape (a cube face or an array layer renders like any 2D
        // image), carrying every aspect so a depth-stencil attachment
        // clears and stores both planes.
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = tex.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = tex.vk_format;
        vi.subresourceRange.aspectMask = tex.aspect;
        vi.subresourceRange.baseMipLevel = mip;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.baseArrayLayer = layer;
        vi.subresourceRange.layerCount = 1;
        if (!vk_check(vkCreateImageView(m_device.handle(), &vi, nullptr, &tex.attachment_views[index]),
                      "vkCreateImageView (attachment)"))
        {
            tex.attachment_views[index] = VK_NULL_HANDLE;
        }
        return tex.attachment_views[index];
    }

    void vk_render_pass_cache::retire_render_pass_variants(vk_render_target& target, bool device_idle)
    {
        if (target.variants.empty())
        {
            return;
        }
        std::vector<uint64_t> generations;
        std::vector<VkRenderPass> render_passes;
        std::vector<VkFramebuffer> framebuffers;
        generations.reserve(target.variants.size());
        render_passes.reserve(target.variants.size());
        for (auto& v : target.variants)
        {
            generations.push_back(v.render_pass_generation);
            if (v.render_pass != VK_NULL_HANDLE)
            {
                render_passes.push_back(v.render_pass);
            }
            for (VkFramebuffer fb : v.framebuffers)
            {
                if (fb != VK_NULL_HANDLE)
                {
                    framebuffers.push_back(fb);
                }
            }
        }
        target.variants.clear();

        // Every graphics pipeline that was bound inside one of these
        // passes cached a VkPipeline built against it. Purge those
        // entries now, matched by generation rather than by handle: a
        // driver may hand a later render pass the same handle value,
        // and a stale entry matched by handle would bind a pipeline
        // built for a pass that no longer exists.
        const auto is_retired = [&generations](const vk_pipeline::variant& pv)
        { return std::find(generations.begin(), generations.end(), pv.render_pass_generation) != generations.end(); };
        std::vector<VkPipeline> pipelines;
        m_pipelines.for_each(
            [&](vk_pipeline& p)
            {
                for (const auto& pv : p.graphics_variants)
                {
                    if (is_retired(pv) && pv.object != VK_NULL_HANDLE)
                    {
                        pipelines.push_back(pv.object);
                    }
                }
                p.graphics_variants.erase(
                    std::remove_if(p.graphics_variants.begin(), p.graphics_variants.end(), is_retired),
                    p.graphics_variants.end());
            });

        const VkDevice dev = m_device.handle();
        if (device_idle)
        {
            // Nothing is in flight and the caller is about to destroy
            // the image views these framebuffers were built on; free
            // them first so no framebuffer ever outlives its attachments.
            for (VkFramebuffer fb : framebuffers)
            {
                vkDestroyFramebuffer(dev, fb, nullptr);
            }
            framebuffers.clear();
        }
        // The render passes and pipelines may still be bound by the
        // previous frame's command buffer when a target is destroyed
        // mid-run (VUID-vkDestroyFramebuffer-framebuffer-00892 and
        // friends), so they always wait for the next fence wait.
        m_frame.enqueue_destroy(
            [dev,
             framebuffers = std::move(framebuffers),
             render_passes = std::move(render_passes),
             pipelines = std::move(pipelines)]
            {
                for (VkPipeline p : pipelines)
                {
                    vkDestroyPipeline(dev, p, nullptr);
                }
                for (VkFramebuffer fb : framebuffers)
                {
                    vkDestroyFramebuffer(dev, fb, nullptr);
                }
                for (VkRenderPass rp : render_passes)
                {
                    vkDestroyRenderPass(dev, rp, nullptr);
                }
            });
    }
} // namespace rendering_engine::gpu::backend::vulkan
