// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_physical_device.hpp
 * @brief The GPU the Vulkan backend runs on: its selection, the queue
 *        families it offers and the formats negotiated against it.
 */

#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_instance;

    // Spelled out rather than taken from the header, which declares it
    // only behind VK_ENABLE_BETA_EXTENSIONS.
    inline constexpr const char* k_portability_subset_extension = "VK_KHR_portability_subset";

    // Picked once the surface exists, since a GPU qualifies only when
    // one of its queue families presents to it. A physical device is
    // never destroyed; reset forgets what quit clears.
    class vk_physical_device
    {
    public:
        // Choose the GPU: API 1.1 or newer, a graphics queue family and
        // a family that presents to the instance's surface, and
        // VK_KHR_swapchain; a discrete GPU wins over any other. Throws
        // when none qualifies.
        void pick_physical_device(const vk_instance& instance);
        // Resolve the three engine depth formats against
        // vkGetPhysicalDeviceFormatProperties through the fallback
        // chains in vk_negotiate.hpp, log the outcome, and throw when a
        // chain resolves to nothing (the spec mandates D32_SFLOAT, so
        // only a broken driver gets there).
        void resolve_depth_formats();
        void reset();

        VkPhysicalDevice handle() const noexcept;
        uint32_t graphics_queue_family() const noexcept;
        uint32_t present_queue_family() const noexcept;
        // timestampValidBits of the graphics queue family: 0 means the
        // queue writes no usable timestamps.
        uint32_t timestamp_valid_bits() const noexcept;
        // The GPU lists VK_EXT_extended_dynamic_state; whether it also
        // has the feature is create_logical_device's question.
        bool has_extended_dynamic_state() const noexcept;
        // The GPU lists VK_KHR_portability_subset, which the spec then
        // requires the logical device to enable.
        bool has_portability_subset() const noexcept;
        // Offset alignment of every staging-ring reservation: the
        // device's optimalBufferCopyOffsetAlignment rounded up to a
        // power of two, at least 16 so any texel block of the engine's
        // formats (and the 4 bytes a depth copy needs) divides it.
        VkDeviceSize copy_offset_alignment() const noexcept;
        // The VkFormat backing @p format on this device. Depth formats
        // come from the fallback chains resolve_depth_formats resolved;
        // colour formats are the fixed translation.
        VkFormat vk_format_for(texture_format format) const noexcept;

    private:
        VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
        uint32_t m_graphics_queue_family{0};
        uint32_t m_present_queue_family{0};
        uint32_t m_timestamp_valid_bits{0};
        bool m_has_extended_dynamic_state{false};
        bool m_has_portability_subset{false};
        VkDeviceSize m_copy_offset_alignment{16};
        // Resolved depth formats, indexed depth24 / depth32_float /
        // depth24_stencil8; see vk_format_for.
        std::array<VkFormat, 3> m_depth_formats{VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
    };
} // namespace rendering_engine::gpu::backend::vulkan
