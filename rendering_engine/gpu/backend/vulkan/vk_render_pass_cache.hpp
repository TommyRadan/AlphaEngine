// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_render_pass_cache.hpp
 * @brief The render passes and framebuffers each render target is
 *        drawn through, built on demand per combination of load and
 *        store ops.
 */

#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/handle_pool.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_frame;
    class vk_logical_device;
    class vk_physical_device;
    class vk_swapchain;

    // A render target keeps its variants (vk_render_target::variant):
    // one VkRenderPass per distinct key and the framebuffers built on
    // it. This builds them on demand, against the textures a target
    // attaches or the swapchain's images, and retires them together
    // with the graphics pipeline variants built against them.
    class vk_render_pass_cache
    {
    public:
        vk_render_pass_cache(const vk_physical_device& physical_device,
                             vk_logical_device& device,
                             vk_frame& frame,
                             const vk_swapchain& swapchain,
                             handle_pool<vk_texture>& textures,
                             handle_pool<vk_pipeline>& pipelines);

        // Acquire (or lazily build) the render pass + framebuffers of
        // @p target for @p key: the per-attachment load / store ops
        // and whether depth takes part. Entries of the key past the
        // target's colour attachment count are ignored.
        VkRenderPass acquire_render_pass(vk_render_target& target, const vk_render_pass_key& key);

        // The single-colour convenience: every colour attachment loads
        // with @p color_load and stores, depth loads with @p depth_load
        // and stores. What the debug overlay asks for to match the
        // debug pass.
        VkRenderPass acquire_render_pass(vk_render_target& target,
                                         VkAttachmentLoadOp color_load,
                                         VkAttachmentLoadOp depth_load,
                                         bool use_depth);

        // Retire every render-pass variant of @p target: its
        // VkRenderPass objects, the framebuffers built on them and —
        // by generation, walking the pipeline pool — every graphics
        // pipeline variant built against them, so no pipeline can be
        // matched to a recycled render-pass handle. Render passes and
        // pipelines go through the deferred-destroy queue (they may be
        // bound by the previous frame's command buffer when a target
        // is destroyed mid-run). With @p device_idle the caller has
        // waited the device idle and is about to destroy the image
        // views the framebuffers reference, so those are freed right
        // here, ahead of their attachments; otherwise they are
        // deferred with the rest.
        void retire_render_pass_variants(vk_render_target& target, bool device_idle);

    private:
        // Lazily create (and cache on the texture) the single-level,
        // single-layer 2D view a framebuffer attaches @p tex through
        // at @p mip / @p layer, carrying every aspect of the format.
        // Returns VK_NULL_HANDLE when the subresource is out of range
        // or the view cannot be created.
        VkImageView attachment_image_view(vk_texture& tex, uint32_t mip, uint32_t layer);

        const vk_physical_device& m_physical_device;
        vk_logical_device& m_device;
        vk_frame& m_frame;
        const vk_swapchain& m_swapchain;
        handle_pool<vk_texture>& m_textures;
        handle_pool<vk_pipeline>& m_pipelines;

        // Source of vk_render_target::variant::render_pass_generation;
        // starts at 1 so 0 can mean "no render pass".
        uint64_t m_next_render_pass_generation{1};
    };
} // namespace rendering_engine::gpu::backend::vulkan
