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
 * @file vk_device_buffer.cpp
 * @brief @c vk_device member functions that manage @c VkBuffer objects.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <cstring>

#include <core/log.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        VkBufferUsageFlags translate_usage(buffer_usage usage)
        {
            VkBufferUsageFlags flags = 0;
            if ((usage & buffer_usage_vertex) != 0u)
            {
                flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            }
            if ((usage & buffer_usage_index) != 0u)
            {
                flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            }
            if ((usage & buffer_usage_uniform) != 0u)
            {
                flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            }
            if ((usage & buffer_usage_storage) != 0u)
            {
                flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            }
            if ((usage & buffer_usage_indirect) != 0u)
            {
                flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            }
            // Always allow staging copies in/out so write_buffer and
            // copy_buffer_to_buffer don't need to know the original
            // descriptor's usage flags.
            flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            return flags;
        }
    } // namespace

    buffer vk_device::create_buffer(const buffer_descriptor& descriptor)
    {
        vk_buffer record{};
        record.size = descriptor.size;
        record.usage = descriptor.usage;
        record.hint = descriptor.hint;

        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = descriptor.size;
        info.usage = translate_usage(descriptor.usage);
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &info, nullptr, &record.object) != VK_SUCCESS)
        {
            LOG_ERR("vkCreateBuffer failed");
            return {};
        }

        VkMemoryRequirements mem_req{};
        vkGetBufferMemoryRequirements(m_device, record.object, &mem_req);

        const VkMemoryPropertyFlags want =
            (descriptor.hint == buffer_usage_hint::static_data)
                ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mem_req.size;
        ai.memoryTypeIndex = find_memory_type(mem_req.memoryTypeBits, want);
        if (vkAllocateMemory(m_device, &ai, nullptr, &record.memory) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_device, record.object, nullptr);
            return {};
        }
        vkBindBufferMemory(m_device, record.object, record.memory, 0);

        if (descriptor.hint != buffer_usage_hint::static_data)
        {
            vkMapMemory(m_device, record.memory, 0, VK_WHOLE_SIZE, 0, &record.mapped);
        }

        if (descriptor.initial_data != nullptr && descriptor.size > 0)
        {
            if (record.mapped != nullptr)
            {
                std::memcpy(record.mapped, descriptor.initial_data, descriptor.size);
            }
            else
            {
                VkBufferCreateInfo si{};
                si.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                si.size = descriptor.size;
                si.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                si.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VkBuffer staging = VK_NULL_HANDLE;
                vkCreateBuffer(m_device, &si, nullptr, &staging);
                VkMemoryRequirements smr{};
                vkGetBufferMemoryRequirements(m_device, staging, &smr);
                VkMemoryAllocateInfo sai{};
                sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                sai.allocationSize = smr.size;
                sai.memoryTypeIndex = find_memory_type(
                    smr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                VkDeviceMemory staging_mem = VK_NULL_HANDLE;
                vkAllocateMemory(m_device, &sai, nullptr, &staging_mem);
                vkBindBufferMemory(m_device, staging, staging_mem, 0);

                void* mapped = nullptr;
                vkMapMemory(m_device, staging_mem, 0, descriptor.size, 0, &mapped);
                std::memcpy(mapped, descriptor.initial_data, descriptor.size);
                vkUnmapMemory(m_device, staging_mem);

                VkCommandBuffer cmd = begin_one_shot();
                VkBufferCopy region{};
                region.size = descriptor.size;
                vkCmdCopyBuffer(cmd, staging, record.object, 1, &region);
                end_one_shot(cmd);

                vkDestroyBuffer(m_device, staging, nullptr);
                vkFreeMemory(m_device, staging_mem, nullptr);
            }
        }

        buffer h{};
        h.id = m_buffers.insert(record);
        return h;
    }

    void vk_device::destroy(buffer handle)
    {
        auto* record = m_buffers.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        // Defer the actual vkDestroy* until the GPU has finished the
        // frame that referenced this buffer. The handle pool slot is
        // freed immediately so the engine can recycle handle ids.
        VkDevice dev = m_device;
        VkBuffer obj = record->object;
        VkDeviceMemory mem = record->memory;
        void* mapped = record->mapped;
        enqueue_destroy(
            [dev, obj, mem, mapped]
            {
                if (mapped != nullptr && mem != VK_NULL_HANDLE)
                {
                    vkUnmapMemory(dev, mem);
                }
                if (obj != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(dev, obj, nullptr);
                }
                if (mem != VK_NULL_HANDLE)
                {
                    vkFreeMemory(dev, mem, nullptr);
                }
            });
        record->object = VK_NULL_HANDLE;
        record->memory = VK_NULL_HANDLE;
        record->mapped = nullptr;
        m_buffers.remove(handle.id);
    }

    void vk_device::write_buffer(buffer handle, const void* data, size_t size, size_t offset)
    {
        auto* record = m_buffers.lookup(handle.id);
        if (record == nullptr || record->object == VK_NULL_HANDLE)
        {
            return;
        }
        if (record->mapped != nullptr)
        {
            std::memcpy(static_cast<uint8_t*>(record->mapped) + offset, data, size);
            return;
        }
        VkBufferCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        si.size = size;
        si.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        si.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer staging = VK_NULL_HANDLE;
        vkCreateBuffer(m_device, &si, nullptr, &staging);
        VkMemoryRequirements smr{};
        vkGetBufferMemoryRequirements(m_device, staging, &smr);
        VkMemoryAllocateInfo sai{};
        sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        sai.allocationSize = smr.size;
        sai.memoryTypeIndex = find_memory_type(
            smr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        vkAllocateMemory(m_device, &sai, nullptr, &staging_mem);
        vkBindBufferMemory(m_device, staging, staging_mem, 0);
        void* mapped = nullptr;
        vkMapMemory(m_device, staging_mem, 0, size, 0, &mapped);
        std::memcpy(mapped, data, size);
        vkUnmapMemory(m_device, staging_mem);

        VkCommandBuffer cmd = begin_one_shot();
        VkBufferCopy region{};
        region.dstOffset = offset;
        region.size = size;
        vkCmdCopyBuffer(cmd, staging, record->object, 1, &region);
        end_one_shot(cmd);

        vkDestroyBuffer(m_device, staging, nullptr);
        vkFreeMemory(m_device, staging_mem, nullptr);
    }
} // namespace rendering_engine::gpu::backend::vulkan
