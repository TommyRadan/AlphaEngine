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
 * @file vk_negotiate.hpp
 * @brief Device-free decisions the Vulkan backend makes at bring-up:
 *        the depth-format fallback chains and the descriptor-pool
 *        budget of each pool in the grow-on-demand chain.
 *
 * Everything here is a pure function over Vulkan's header constants
 * — no @c VkDevice, no loader call — so @c vk_device can resolve its
 * tables once at init and the unit tests can pin the chains without a
 * GPU. The caller supplies the format-support query (backed by
 * @c vkGetPhysicalDeviceFormatProperties in the device, by a table in
 * the tests).
 */

#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    // The VkFormats that may back an engine depth format, most
    // preferred first. Vulkan only mandates D32_SFLOAT (and D16) as a
    // depth attachment, one of X8_D24 / D32_SFLOAT, and one of D24S8 /
    // D32S8, so the packed 24-bit formats the engine defaults to need
    // a fallback on hardware that omits them (MoltenVK, much mobile).
    // Empty for a colour format.
    std::span<const VkFormat> depth_format_candidates(texture_format format);

    // True when @p format may back a depth/stencil attachment with
    // optimal tiling. What the device answers from
    // vkGetPhysicalDeviceFormatProperties; the tests answer from a
    // table.
    using depth_attachment_query = std::function<bool(VkFormat)>;

    // The first candidate of @ref depth_format_candidates that
    // @p supports_depth_attachment accepts, or VK_FORMAT_UNDEFINED
    // when none does (or @p format is not a depth format).
    VkFormat select_depth_format(texture_format format, const depth_attachment_query& supports_depth_attachment);

    // The image aspect a resolved VkFormat carries: depth only, depth
    // + stencil, or colour. Used for the image view and the layout
    // barriers of a texture whose VkFormat was picked by the fallback
    // chain rather than derived from its texture_format.
    VkImageAspectFlags aspect_for_vk_format(VkFormat format);

    // Per-descriptor-type capacity of one pool in the chain.
    struct descriptor_pool_budget
    {
        uint32_t max_sets{0};
        uint32_t uniform_buffers{0};
        uint32_t combined_image_samplers{0};
        uint32_t storage_buffers{0};
        uint32_t storage_images{0};
    };

    // Budget of the @p pool_index-th pool (0-based). The first pool
    // holds a base budget sized for the engine's material sets (one
    // UBO + up to eight samplers each, plus a per-draw UBO set per
    // renderable); every later pool doubles the previous one until
    // the growth cap, so a scene that outgrows the first pool settles
    // into a few large pools instead of a long chain of small ones.
    descriptor_pool_budget descriptor_pool_budget_for(uint32_t pool_index);
} // namespace rendering_engine::gpu::backend::vulkan
