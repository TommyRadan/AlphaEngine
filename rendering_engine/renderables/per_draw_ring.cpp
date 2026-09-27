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

#include <rendering_engine/renderables/per_draw_ring.hpp>

#include <algorithm>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/renderables/draw_item.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/transform.hpp>
#include <runtime/engine.hpp>

namespace
{
    // Offsets are std140 blocks, so a slot is at least 16-byte aligned
    // even on a device that reports a smaller (or no) alignment.
    constexpr uint32_t min_slot_alignment = 16;

    // Largest region the ring regrows to. Past it a frame that still
    // overflows keeps spilling every frame: slower, never wrong. Keeps
    // every offset well inside the 32-bit dynamic offset.
    constexpr uint32_t max_slots_per_frame = 1u << 20;

    uint32_t round_up(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }
} // namespace

namespace rendering_engine
{
    per_draw_ring::per_draw_ring(gpu::device& device) : m_device(device)
    {
        const uint32_t alignment = std::max(device.limits().uniform_buffer_offset_alignment, min_slot_alignment);
        m_stride = round_up(static_cast<uint32_t>(per_draw_ubo_size), alignment);
        m_slots_per_frame = initial_slots_per_frame;
        // One region per frame the device keeps in flight; fixed for the
        // device's lifetime, like the slot the device hands out.
        m_frames_in_flight = std::max(device.frames_in_flight(), 1u);
        m_region = device.frame_slot() % m_frames_in_flight;
        // With push constants no rigid draw allocates here. Should
        // something allocate anyway, that frame spills and the next
        // begin_frame creates the main buffer at the grown size.
        m_push_constants = per_draw_push_constants(device);
        if (!m_push_constants)
        {
            m_main.buffer = create_buffer(m_slots_per_frame * m_frames_in_flight, "per_draw_ring");
        }
    }

    per_draw_ring::~per_draw_ring()
    {
        for (auto& spill : m_spill)
        {
            release(spill);
        }
        m_spill.clear();
        release(m_main);
    }

    gpu::buffer per_draw_ring::create_buffer(uint32_t slots, const char* name)
    {
        // Host-visible: the Vulkan backend maps a stream buffer for its
        // whole lifetime, so each block is a memcpy into it. Stream, not
        // dynamic: the ring partitions the buffer per frame in flight
        // itself, so the backend keeps a single copy (see
        // buffer_usage_hint).
        gpu::buffer_descriptor descriptor{};
        descriptor.size = static_cast<size_t>(slots) * m_stride;
        descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        descriptor.hint = gpu::buffer_usage_hint::stream_data;
        const gpu::buffer buffer = m_device.create_buffer(descriptor);
        if (!buffer.valid())
        {
            if (!m_failure_reported)
            {
                m_failure_reported = true;
                LOG_ERR("per_draw_ring: could not create a %zu-byte uniform buffer; draws without a slot are skipped",
                        descriptor.size);
            }
            return {};
        }
        m_device.set_debug_name(buffer, name);
        return buffer;
    }

    void per_draw_ring::release(chunk& target)
    {
        for (const auto& entry : target.groups)
        {
            m_device.destroy(entry.group);
        }
        target.groups.clear();
        if (target.buffer.valid())
        {
            m_device.destroy(target.buffer);
            target.buffer = {};
        }
    }

    gpu::bind_group per_draw_ring::group_for(chunk& target, gpu::bind_group_layout layout)
    {
        for (auto& entry : target.groups)
        {
            if (entry.layout == layout)
            {
                entry.last_used = m_frame_serial;
                return entry.group;
            }
        }
        // First draw under this layout from this buffer: the one group
        // every later draw shares. Written once, never updated.
        const gpu::bind_group group = create_per_draw_bind_group(m_device, layout, target.buffer);
        if (group.valid())
        {
            target.groups.push_back({layout, group, m_frame_serial});
        }
        return group;
    }

    per_draw_ring::chunk* per_draw_ring::spill_chunk()
    {
        if (m_spill.empty() || m_spill_cursor >= m_slots_per_frame)
        {
            chunk spill{};
            spill.buffer = create_buffer(m_slots_per_frame, "per_draw_ring_spill");
            if (!spill.buffer.valid())
            {
                return nullptr;
            }
            m_spill.push_back(std::move(spill));
            m_spill_cursor = 0;
        }
        return &m_spill.back();
    }

    void per_draw_ring::begin_frame()
    {
        ++m_frame_serial;

        if (!m_spill.empty())
        {
            // Last frame outgrew its region. Its spill buffers go (the
            // device defers the free until the GPU is done with them), and
            // the main buffer regrows to what the frame used plus half
            // again, so a scene that grew settles after one spilled frame.
            for (auto& spill : m_spill)
            {
                release(spill);
            }
            m_spill.clear();

            const uint64_t wanted = static_cast<uint64_t>(m_used) + m_used / 2;
            const auto slots = static_cast<uint32_t>(
                std::min<uint64_t>(std::max<uint64_t>(wanted, uint64_t{m_slots_per_frame} * 2), max_slots_per_frame));
            if (slots > m_slots_per_frame)
            {
                release(m_main);
                m_slots_per_frame = slots;
                m_main.buffer = create_buffer(m_slots_per_frame * m_frames_in_flight, "per_draw_ring");
                LOG_INF("per_draw_ring: grew to %u draws per frame (%u bytes per slot) after a frame used %u",
                        m_slots_per_frame,
                        m_stride,
                        m_used);
            }
        }

        // Release the main buffer's groups whose layout has stopped
        // drawing; an allocation made under them belongs to a past frame,
        // so no draw still to be recorded refers to them.
        std::erase_if(m_main.groups,
                      [this](const chunk::layout_group& entry)
                      {
                          if (m_frame_serial - entry.last_used <= idle_frames_before_release)
                          {
                              return false;
                          }
                          m_device.destroy(entry.group);
                          return true;
                      });

        // The region of the slot the device is recording into: its fence
        // was waited in device::begin_frame, so the GPU is done with it.
        m_region = m_device.frame_slot() % m_frames_in_flight;
        m_cursor = 0;
        m_spill_cursor = 0;
        m_used = 0;
    }

    per_draw_allocation per_draw_ring::allocate(gpu::bind_group_layout layout, const per_draw_payload& payload)
    {
        if (!layout.valid())
        {
            return {};
        }

        chunk* target = nullptr;
        size_t offset = 0;
        const bool in_main = m_main.buffer.valid() && m_cursor < m_slots_per_frame;
        if (in_main)
        {
            target = &m_main;
            offset = (static_cast<size_t>(m_region) * m_slots_per_frame + m_cursor) * m_stride;
        }
        else
        {
            target = spill_chunk();
            if (target == nullptr)
            {
                return {};
            }
            offset = static_cast<size_t>(m_spill_cursor) * m_stride;
        }

        const gpu::bind_group group = group_for(*target, layout);
        if (!group.valid())
        {
            return {};
        }

        // The slot is taken only once the draw can use it.
        if (in_main)
        {
            ++m_cursor;
        }
        else
        {
            ++m_spill_cursor;
        }
        ++m_used;

        m_device.write_buffer(target->buffer, &payload, per_draw_ubo_size, offset);
        return {group, static_cast<uint32_t>(offset)};
    }

    bool per_draw_binding::refresh(const util::transform& transform)
    {
        const uint64_t version = transform.get_world_version();
        if (version == m_world_version)
        {
            return false;
        }
        m_payload = make_per_draw_payload(transform.get_world_matrix());
        m_mirrored = is_mirrored(m_payload.model);
        m_world_version = version;
        return true;
    }

    bool per_draw_binding::bind(const util::transform& transform, gpu::bind_group_layout layout, draw_item& item)
    {
        per_draw_ring& ring = runtime::current_engine().renderer->get_per_draw_ring();

        const bool changed = refresh(transform);
        if (ring.uses_push_constants())
        {
            // The pass pushes the cached block right before the draw; the
            // bytes are copied into the command stream there, so a draw an
            // earlier pass recorded keeps the block it pushed.
            item.per_draw_push = &m_payload;
            item.per_draw_bind_group = {};
            item.per_draw_dynamic = false;
            item.mirrored = m_mirrored;
            return true;
        }

        // A block that changed needs a fresh slot even mid-frame: an
        // earlier pass may already have recorded a draw that reads the
        // old one.
        if (changed || m_frame != ring.frame_serial() || m_layout != layout || !m_allocation.bind_group.valid())
        {
            m_allocation = ring.allocate(layout, m_payload);
            m_layout = layout;
            m_frame = ring.frame_serial();
            if (!m_allocation.bind_group.valid())
            {
                return false;
            }
        }

        item.per_draw_bind_group = m_allocation.bind_group;
        item.per_draw_offset = m_allocation.offset;
        item.per_draw_dynamic = true;
        item.mirrored = m_mirrored;
        return true;
    }
} // namespace rendering_engine
