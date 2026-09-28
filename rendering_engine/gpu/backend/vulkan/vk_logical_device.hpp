// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_logical_device.hpp
 * @brief The Vulkan logical device: its queues, the optional features
 *        and extensions negotiated for it, the memory allocator over it,
 *        and whether it has been lost.
 */

#pragma once

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/vulkan/vk_allocator.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_instance;
    class vk_physical_device;

    // Created on the picked physical device and destroyed after every
    // object made from it; every other component reaches the VkDevice,
    // its queues and the allocator through here, and routes the
    // results of queue-level calls through check_queue_result so a
    // device loss is recorded once, wherever it surfaces.
    class vk_logical_device
    {
    public:
        vk_logical_device(const vk_instance& instance, const vk_physical_device& physical_device);

        // Create the VkDevice with one queue per distinct family and
        // every optional core feature the physical device reports,
        // recording what was granted in @p grants for the consumers to
        // gate on (each gap is logged once). Throws when
        // vkCreateDevice fails.
        void create_logical_device(device_features& grants);
        // The VMA allocator over the logical device, told the API
        // version the instance and the physical device agree on.
        // Throws when VMA refuses; destroyed after every allocation.
        void create_allocator();
        void destroy_allocator();
        // vkDestroyDevice, once nothing made from the device is left.
        void destroy();

        VkDevice handle() const noexcept;
        VkQueue graphics_queue() const noexcept;
        VkQueue present_queue() const noexcept;
        // The memory allocator every buffer and image is allocated
        // from; alive from create_allocator until destroy_allocator.
        VmaAllocator allocator() const noexcept;
        // VK_EXT_extended_dynamic_state is enabled and its
        // vkCmdBindVertexBuffers2EXT resolved.
        bool extended_dynamic_state_enabled() const noexcept;
        PFN_vkCmdBindVertexBuffers2EXT cmd_bind_vertex_buffers2() const noexcept;

        // True once a queue operation reported VK_ERROR_DEVICE_LOST.
        bool device_lost() const noexcept;
        // Record a device loss reported by @p what: logs once at fatal
        // level and flips device_lost.
        void mark_device_lost(const char* what);
        // Result check for the queue-level calls that can report
        // VK_ERROR_DEVICE_LOST (submit, present, fence and idle waits):
        // that code goes through mark_device_lost, any other failure is
        // logged through vk_check. Returns true on VK_SUCCESS.
        bool check_queue_result(VkResult result, const char* what);
        // Forget a loss (quit, for the next init).
        void reset();

    private:
        const vk_instance& m_instance;
        const vk_physical_device& m_physical_device;

        VkDevice m_device{VK_NULL_HANDLE};
        VkQueue m_graphics_queue{VK_NULL_HANDLE};
        VkQueue m_present_queue{VK_NULL_HANDLE};
        VmaAllocator m_allocator{VK_NULL_HANDLE};
        // VK_EXT_extended_dynamic_state — needed for runtime stride
        // override on @c set_vertex_buffer. See
        // vk_device::extended_dynamic_state_enabled for the rationale.
        bool m_extended_dynamic_state_enabled{false};
        PFN_vkCmdBindVertexBuffers2EXT m_cmd_bind_vertex_buffers2{nullptr};
        bool m_device_lost{false};
    };
} // namespace rendering_engine::gpu::backend::vulkan
