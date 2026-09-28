// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_descriptor_allocator.hpp
 * @brief The grow-on-demand chain of descriptor pools every bind
 *        group's descriptor sets come from.
 */

#pragma once

#include <vector>

#include <vulkan/vulkan.h>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_logical_device;

    // Allocations come from the newest pool, sets are freed to the pool
    // recorded on their bind group, and the whole chain is destroyed at
    // quit, after every descriptor set.
    class vk_descriptor_allocator
    {
    public:
        explicit vk_descriptor_allocator(const vk_logical_device& device);

        // Append one pool to the chain, sized by
        // descriptor_pool_budget_for(chain length). Returns false with
        // an error logged when the driver refuses.
        bool create_descriptor_pool();

        // Allocate one descriptor set of @p layout from the pool chain:
        // the newest pool first, and when it is exhausted
        // (VK_ERROR_OUT_OF_POOL_MEMORY / VK_ERROR_FRAGMENTED_POOL) a
        // fresh, larger pool is appended and the allocation retried
        // once. @p out_pool receives the pool the set came from, which
        // is the only pool it may be freed back to. Returns false, with
        // an error logged, when even the fresh pool refuses.
        bool
        allocate_descriptor_set(VkDescriptorSetLayout layout, VkDescriptorSet& out_set, VkDescriptorPool& out_pool);

        // Destroy every pool of the chain (and with them any set still
        // allocated from one).
        void destroy();

    private:
        const vk_logical_device& m_device;
        std::vector<VkDescriptorPool> m_descriptor_pools;
    };
} // namespace rendering_engine::gpu::backend::vulkan
