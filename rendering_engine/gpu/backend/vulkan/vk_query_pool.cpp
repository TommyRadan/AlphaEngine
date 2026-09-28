// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_query_pool.cpp
 * @brief @c vk_query_pool: the timestamp query sets.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_query_pool.hpp>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_frame.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    vk_query_pool::vk_query_pool(vk_logical_device& device, vk_frame& frame) : m_device{device}, m_frame{frame} {}

    query_set vk_query_pool::create_query_set(const query_set_descriptor& descriptor, bool timestamp_queries)
    {
        if (!timestamp_queries)
        {
            LOG_WRN("create_query_set: the graphics queue writes no timestamps; no query set created");
            return {};
        }
        if (descriptor.count == 0)
        {
            LOG_ERR("create_query_set: a query set needs at least one query");
            return {};
        }
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = descriptor.count;
        vk_query_set record{};
        record.count = descriptor.count;
        if (!vk_check(vkCreateQueryPool(m_device.handle(), &info, nullptr, &record.pool), "vkCreateQueryPool"))
        {
            return {};
        }
        query_set h{};
        h.id = m_query_sets.insert(record);
        return h;
    }

    void vk_query_pool::destroy(query_set handle)
    {
        auto* record = m_query_sets.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        // The frame's command buffer may still write or reset the
        // pool; it goes with the rest at the next fence wait.
        const VkDevice dev = m_device.handle();
        const VkQueryPool pool = record->pool;
        if (pool != VK_NULL_HANDLE)
        {
            m_frame.enqueue_destroy([dev, pool] { vkDestroyQueryPool(dev, pool, nullptr); });
        }
        record->pool = VK_NULL_HANDLE;
        m_query_sets.remove(handle.id);
    }

    bool vk_query_pool::resolve_queries(query_set set, uint32_t first, uint32_t count, uint64_t* out_ticks)
    {
        auto* record = m_query_sets.lookup(set.id);
        if (record == nullptr || record->pool == VK_NULL_HANDLE || out_ticks == nullptr || count == 0)
        {
            return false;
        }
        if (first > record->count || count > record->count - first)
        {
            LOG_WRN("resolve_queries: %u queries from %u exceed the %u-query set", count, first, record->count);
            return false;
        }
        if (m_device.device_lost())
        {
            return false;
        }
        // No wait: VK_NOT_READY means a query has not completed (or was
        // reset and never written) and the caller keeps its previous
        // values.
        const VkResult r = vkGetQueryPoolResults(m_device.handle(),
                                                 record->pool,
                                                 first,
                                                 count,
                                                 static_cast<size_t>(count) * sizeof(uint64_t),
                                                 out_ticks,
                                                 sizeof(uint64_t),
                                                 VK_QUERY_RESULT_64_BIT);
        if (r == VK_NOT_READY)
        {
            return false;
        }
        return m_device.check_queue_result(r, "vkGetQueryPoolResults");
    }

    vk_query_set* vk_query_pool::lookup_query_set(query_set h)
    {
        return m_query_sets.lookup(h.id);
    }

    void vk_query_pool::destroy_all()
    {
        m_query_sets.for_each(
            [&](vk_query_set& q)
            {
                if (q.pool != VK_NULL_HANDLE)
                {
                    vkDestroyQueryPool(m_device.handle(), q.pool, nullptr);
                    q.pool = VK_NULL_HANDLE;
                }
            });
        m_query_sets.clear();
    }
} // namespace rendering_engine::gpu::backend::vulkan
