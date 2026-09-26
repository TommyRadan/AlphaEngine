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

#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>

#include <algorithm>
#include <array>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        // depth32_float: D32_SFLOAT is a mandatory depth attachment
        // format, so the chain is one entry deep; the stencil variant
        // is listed only so a driver that somehow refuses the plain
        // format still resolves to 32-bit float depth.
        constexpr std::array<VkFormat, 2> k_depth32_float_chain{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT};

        // depth24: the packed 24-bit format is optional; D32_SFLOAT
        // is the mandatory stand-in (same aspect, same non-linear
        // encoding the depth consumers decode), and the two stencil
        // formats are a last resort past what the spec can reach.
        constexpr std::array<VkFormat, 4> k_depth24_chain{VK_FORMAT_X8_D24_UNORM_PACK32,
                                                          VK_FORMAT_D32_SFLOAT,
                                                          VK_FORMAT_D24_UNORM_S8_UINT,
                                                          VK_FORMAT_D32_SFLOAT_S8_UINT};

        // depth24_stencil8: the spec mandates one of the two, so this
        // chain always resolves.
        constexpr std::array<VkFormat, 2> k_depth24_stencil8_chain{VK_FORMAT_D24_UNORM_S8_UINT,
                                                                   VK_FORMAT_D32_SFLOAT_S8_UINT};

        // Growth of the descriptor-pool chain: the first pool holds
        // the base budget, each later pool doubles it, capped so a
        // runaway leak grows in bounded steps.
        constexpr uint32_t k_base_sets = 256;
        constexpr uint32_t k_base_uniform_buffers = 512;
        constexpr uint32_t k_base_combined_image_samplers = 2048;
        constexpr uint32_t k_base_storage_buffers = 64;
        constexpr uint32_t k_base_storage_images = 64;
        constexpr uint32_t k_max_growth_shift = 4;
    } // namespace

    std::span<const VkFormat> depth_format_candidates(texture_format format)
    {
        switch (format)
        {
        case texture_format::depth32_float:
            return k_depth32_float_chain;
        case texture_format::depth24:
            return k_depth24_chain;
        case texture_format::depth24_stencil8:
            return k_depth24_stencil8_chain;
        default:
            return {};
        }
    }

    VkFormat select_depth_format(texture_format format, const depth_attachment_query& supports_depth_attachment)
    {
        if (!supports_depth_attachment)
        {
            return VK_FORMAT_UNDEFINED;
        }
        for (const VkFormat candidate : depth_format_candidates(format))
        {
            if (supports_depth_attachment(candidate))
            {
                return candidate;
            }
        }
        return VK_FORMAT_UNDEFINED;
    }

    VkImageAspectFlags aspect_for_vk_format(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    descriptor_pool_budget descriptor_pool_budget_for(uint32_t pool_index)
    {
        const uint32_t scale = 1u << std::min(pool_index, k_max_growth_shift);
        descriptor_pool_budget budget{};
        budget.max_sets = k_base_sets * scale;
        budget.uniform_buffers = k_base_uniform_buffers * scale;
        budget.combined_image_samplers = k_base_combined_image_samplers * scale;
        budget.storage_buffers = k_base_storage_buffers * scale;
        budget.storage_images = k_base_storage_images * scale;
        return budget;
    }
} // namespace rendering_engine::gpu::backend::vulkan
