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

        // Static data lives in device-local memory and is filled through
        // the staging ring; anything rewritten from the host (dynamic /
        // stream) is host-visible, host-coherent and mapped for its
        // whole lifetime by its allocation. VMA sub-allocates both from
        // its memory blocks, so a buffer is not a vkAllocateMemory of
        // its own.
        const bool host_visible = descriptor.hint != buffer_usage_hint::static_data;
        const VmaAllocationCreateInfo alloc = host_visible ? host_mapped_allocation(false) : device_local_allocation();
        VmaAllocationInfo alloc_info{};
        if (!vk_check(vmaCreateBuffer(m_allocator, &info, &alloc, &record.object, &record.allocation, &alloc_info),
                      "vmaCreateBuffer"))
        {
            return {};
        }
        if (host_visible)
        {
            // A host-visible buffer that is not mapped would drop every
            // later write_buffer, so it is not handed out.
            record.mapped = alloc_info.pMappedData;
            if (record.mapped == nullptr)
            {
                LOG_ERR("vk_device::create_buffer: host-visible buffer allocation is not mapped");
                vmaDestroyBuffer(m_allocator, record.object, record.allocation);
                return {};
            }
        }

        if (descriptor.initial_data != nullptr && descriptor.size > 0)
        {
            if (record.mapped != nullptr)
            {
                std::memcpy(record.mapped, descriptor.initial_data, descriptor.size);
            }
            else
            {
                // Device-local: stage the data and record the copy into
                // the open transfer batch. A failed staging step leaves
                // the buffer allocated but unfilled, with the failure
                // logged.
                staged_upload source{};
                if (stage_upload(descriptor.initial_data, descriptor.size, source))
                {
                    record_buffer_copy(source, record.object, 0, descriptor.size);
                }
                else
                {
                    LOG_ERR("vk_device::create_buffer: initial data not uploaded (staging failed)");
                }
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
        // Defer the actual destruction until the GPU has finished the
        // frame (and the transfer batch) that referenced this buffer.
        // The handle pool slot is freed immediately so the engine can
        // recycle handle ids. The persistent map goes with the
        // allocation.
        const VmaAllocator allocator = m_allocator;
        const VkBuffer obj = record->object;
        const VmaAllocation allocation = record->allocation;
        enqueue_destroy(
            [allocator, obj, allocation]
            {
                if (obj != VK_NULL_HANDLE)
                {
                    vmaDestroyBuffer(allocator, obj, allocation);
                }
            });
        record->object = VK_NULL_HANDLE;
        record->allocation = VK_NULL_HANDLE;
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
        if (data == nullptr || size == 0)
        {
            return;
        }
        // Device-local: through the staging ring into the open transfer
        // batch, which submit() queues ahead of the frame that reads
        // the buffer.
        staged_upload source{};
        if (!stage_upload(data, size, source))
        {
            LOG_ERR("vk_device::write_buffer: %zu bytes not written (staging failed)", size);
            return;
        }
        record_buffer_copy(source, record->object, offset, size);
    }
} // namespace rendering_engine::gpu::backend::vulkan
