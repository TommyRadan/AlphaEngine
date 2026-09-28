// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_descriptor_allocator.cpp
 * @brief @c vk_descriptor_allocator: the descriptor pool chain.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_descriptor_allocator.hpp>

#include <array>
#include <cstdint>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    vk_descriptor_allocator::vk_descriptor_allocator(const vk_logical_device& device) : m_device{device} {}

    bool vk_descriptor_allocator::create_descriptor_pool()
    {
        const uint32_t index = static_cast<uint32_t>(m_descriptor_pools.size());
        const descriptor_pool_budget budget = descriptor_pool_budget_for(index);
        std::array<VkDescriptorPoolSize, 5> sizes{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = budget.uniform_buffers;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = budget.combined_image_samplers;
        sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[2].descriptorCount = budget.storage_buffers;
        sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[3].descriptorCount = budget.storage_images;
        sizes[4].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        sizes[4].descriptorCount = budget.dynamic_uniform_buffers;

        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets = budget.max_sets;
        info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (!vk_check(vkCreateDescriptorPool(m_device.handle(), &info, nullptr, &pool), "vkCreateDescriptorPool"))
        {
            return false;
        }
        m_descriptor_pools.push_back(pool);
        LOG_INF("Vulkan descriptor pool %u: %u sets (%u uniform buffers, %u dynamic uniform buffers, "
                "%u combined image samplers, %u storage buffers, %u storage images)",
                index,
                budget.max_sets,
                budget.uniform_buffers,
                budget.dynamic_uniform_buffers,
                budget.combined_image_samplers,
                budget.storage_buffers,
                budget.storage_images);
        return true;
    }

    bool vk_descriptor_allocator::allocate_descriptor_set(VkDescriptorSetLayout layout,
                                                          VkDescriptorSet& out_set,
                                                          VkDescriptorPool& out_pool)
    {
        out_set = VK_NULL_HANDLE;
        out_pool = VK_NULL_HANDLE;
        if (m_descriptor_pools.empty() && !create_descriptor_pool())
        {
            return false;
        }
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = m_descriptor_pools.back();
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;
            const VkResult r = vkAllocateDescriptorSets(m_device.handle(), &ai, &out_set);
            if (r == VK_SUCCESS)
            {
                out_pool = m_descriptor_pools.back();
                return true;
            }
            out_set = VK_NULL_HANDLE;
            // Out-of-pool-memory and fragmentation are the two "this
            // pool is full" answers; anything else is a real failure.
            // The fresh pool is larger than the one that ran out, so a
            // second refusal from it is not retried.
            const bool exhausted = r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL;
            if (!exhausted || attempt == 1)
            {
                vk_check(r, "vkAllocateDescriptorSets");
                return false;
            }
            if (!create_descriptor_pool())
            {
                return false;
            }
        }
        return false;
    }

    void vk_descriptor_allocator::destroy()
    {
        for (VkDescriptorPool pool : m_descriptor_pools)
        {
            if (pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_device.handle(), pool, nullptr);
            }
        }
        m_descriptor_pools.clear();
    }
} // namespace rendering_engine::gpu::backend::vulkan
