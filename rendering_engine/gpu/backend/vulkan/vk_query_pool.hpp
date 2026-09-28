// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_query_pool.hpp
 * @brief The timestamp query sets: one VkQueryPool per
 *        @ref gpu::query_set, and the non-blocking readback of its
 *        results.
 */

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/backend/handle_pool.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_frame;
    class vk_logical_device;

    class vk_query_pool
    {
    public:
        vk_query_pool(vk_logical_device& device, vk_frame& frame);

        // A timestamp query set of descriptor.count queries; none (with
        // a warning) when @p timestamp_queries says the graphics queue
        // writes no timestamps.
        query_set create_query_set(const query_set_descriptor& descriptor, bool timestamp_queries);
        // Retire the set; its pool goes through the deferred-destroy
        // queue, since the frame's command buffer may still write it.
        void destroy(query_set handle);
        // Copy @p count results from @p first without waiting; false
        // when a query has not completed yet (the caller keeps its
        // previous values) or the device is lost.
        bool resolve_queries(query_set set, uint32_t first, uint32_t count, uint64_t* out_ticks);
        vk_query_set* lookup_query_set(query_set h);

        // Destroy every live set's pool and forget the sets (quit,
        // under vkDeviceWaitIdle).
        void destroy_all();

    private:
        vk_logical_device& m_device;
        vk_frame& m_frame;
        handle_pool<vk_query_set> m_query_sets;
    };
} // namespace rendering_engine::gpu::backend::vulkan
