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
 * @brief @c vk_device member functions that manage @c VkBuffer objects,
 *        including the per-frame-slot regions of a dynamic buffer.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <algorithm>
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

        VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
        {
            const VkDeviceSize remainder = value % alignment;
            return remainder == 0 ? value : value + (alignment - remainder);
        }

        // Widen @p gap to cover [begin, end).
        void extend_gap(vk_buffer::region_gap& gap, VkDeviceSize begin, VkDeviceSize end)
        {
            if (gap.empty())
            {
                gap.begin = begin;
                gap.end = end;
                return;
            }
            gap.begin = std::min(gap.begin, begin);
            gap.end = std::max(gap.end, end);
        }
    } // namespace

    buffer vk_device::create_buffer(const buffer_descriptor& descriptor)
    {
        vk_buffer record{};
        record.size = descriptor.size;
        record.usage = descriptor.usage;
        record.hint = descriptor.hint;

        // Static data lives in device-local memory and is filled through
        // the staging ring; anything rewritten from the host (dynamic /
        // stream) is host-visible, host-coherent and mapped for its
        // whole lifetime by its allocation. VMA sub-allocates both from
        // its memory blocks, so a buffer is not a vkAllocateMemory of
        // its own.
        //
        // A dynamic_data buffer on a device with several frames in
        // flight is allocated once per frame slot, the copies laid out
        // back to back (see vk_buffer::region_count): the host writes
        // the current slot's copy while the frames in flight read
        // theirs, and each copy is brought up to date as its slot comes
        // round. A copy starts where a uniform or storage descriptor may
        // point, so the stride is the size rounded up to both offset
        // alignments. A stream_data buffer is one copy the caller
        // partitions itself.
        const bool host_visible = descriptor.hint != buffer_usage_hint::static_data;
        record.region_count = descriptor.hint == buffer_usage_hint::dynamic_data ? m_frames_in_flight : 1;
        record.region_stride = descriptor.size;
        if (record.region_count > 1)
        {
            const VkDeviceSize alignment = std::max<VkDeviceSize>(
                {m_limits.uniform_buffer_offset_alignment, m_limits.storage_buffer_offset_alignment, 16});
            record.region_stride = align_up(descriptor.size, alignment);
        }

        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = record.region_stride * record.region_count;
        info.usage = translate_usage(descriptor.usage);
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

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
                // Nothing reads a new buffer yet, so every copy starts
                // complete and no region lags another.
                for (uint32_t region = 0; region < record.region_count; ++region)
                {
                    std::memcpy(static_cast<uint8_t*>(record.mapped) + region * record.region_stride,
                                descriptor.initial_data,
                                descriptor.size);
                }
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
        if (data == nullptr || size == 0)
        {
            return;
        }
        if (offset > record->size || size > record->size - offset)
        {
            // A multi-buffered write past the end would land in the next
            // slot's copy; a single-copy one past the allocation.
            LOG_ERR("vk_device::write_buffer: %zu bytes at offset %zu exceed the %zu-byte buffer",
                    size,
                    offset,
                    record->size);
            return;
        }
        if (record->mapped != nullptr)
        {
            auto* mapped = static_cast<uint8_t*>(record->mapped);
            if (record->region_count <= 1)
            {
                std::memcpy(mapped + offset, data, size);
                return;
            }
            // Into the current slot's copy, which no frame in flight
            // reads: first whatever it has missed since it was last
            // written (bar the span this write replaces), then the new
            // bytes. The other copies now lag by this span.
            wait_slot_before_host_write();
            const uint32_t slot = m_frame_slot;
            const VkDeviceSize begin = offset;
            const VkDeviceSize end = offset + size;
            sync_host_region(*record, slot, begin, end);
            std::memcpy(mapped + slot * record->region_stride + offset, data, size);
            record->latest_region = slot;
            for (uint32_t region = 0; region < record->region_count; ++region)
            {
                if (region != slot)
                {
                    extend_gap(record->gaps[region], begin, end);
                }
            }
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

    void vk_device::sync_host_region(vk_buffer& record, uint32_t slot, VkDeviceSize skip_begin, VkDeviceSize skip_end)
    {
        if (record.region_count <= 1 || slot >= record.region_count || record.mapped == nullptr)
        {
            return;
        }
        vk_buffer::region_gap& gap = record.gaps[slot];
        if (gap.empty())
        {
            return;
        }
        if (record.latest_region != slot)
        {
            // The latest region is complete and only read by the GPU,
            // so reading it from the host races nothing. The gap is
            // one span; the part the caller is about to overwrite is
            // skipped, which leaves at most a head and a tail.
            const auto* mapped = static_cast<const uint8_t*>(record.mapped);
            const uint8_t* source = mapped + record.latest_region * record.region_stride;
            auto* target = static_cast<uint8_t*>(record.mapped) + slot * record.region_stride;
            const VkDeviceSize head_end = std::min(gap.end, skip_begin);
            if (head_end > gap.begin)
            {
                std::memcpy(target + gap.begin, source + gap.begin, head_end - gap.begin);
            }
            const VkDeviceSize tail_begin = std::max(gap.begin, skip_end);
            if (gap.end > tail_begin)
            {
                std::memcpy(target + tail_begin, source + tail_begin, gap.end - tail_begin);
            }
        }
        gap = {};
    }

    VkDeviceSize vk_device::host_region_offset(const vk_buffer& record) const noexcept
    {
        return record.region_count > 1 ? m_frame_slot * record.region_stride : 0;
    }

    void vk_device::ensure_host_region_current(vk_buffer& record)
    {
        if (record.region_count <= 1 || record.mapped == nullptr || record.gaps[m_frame_slot].empty())
        {
            return;
        }
        wait_slot_before_host_write();
        sync_host_region(record, m_frame_slot, 0, 0);
    }

    void vk_device::prepare_bind_group(vk_bind_group& group)
    {
        if (group.set_count <= 1)
        {
            // A single set binds no multi-buffered buffer.
            return;
        }
        for (const binding_value& entry : group.entries)
        {
            if (entry.kind != binding_kind::uniform_buffer && entry.kind != binding_kind::storage_buffer)
            {
                continue;
            }
            if (auto* record = m_buffers.lookup(entry.buffer_value.id))
            {
                ensure_host_region_current(*record);
            }
        }
    }
} // namespace rendering_engine::gpu::backend::vulkan
