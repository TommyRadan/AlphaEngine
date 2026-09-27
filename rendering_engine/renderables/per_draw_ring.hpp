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
 * @file per_draw_ring.hpp
 * @brief The per-frame linear allocator the 3D renderables write their
 *        PerDraw block into, and the per-renderable cache in front of it.
 *
 * Rather than each rigid 3D renderable owning a 128-byte uniform buffer
 * and a bind group over it, the ring keeps one large host-visible uniform
 * buffer (persistently mapped on Vulkan, where a write is a plain memcpy)
 * and one bind group per per-draw layout: a draw copies its block into
 * the next slot of the frame's region and binds the shared group with the
 * slot's byte offset as a dynamic offset (@ref draw_item::per_draw_offset).
 * The descriptor sets are written once, when a group is first asked for,
 * and never updated per draw.
 *
 * That is the path of a device without push constants (OpenGL). On one
 * with them (Vulkan) the renderables push their block instead (see
 * per_draw_ubo.hpp) and nothing is written into the ring; it stays for
 * blocks the push range cannot take.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }
    namespace util
    {
        struct transform;
    }
    struct draw_item;

    // One slot handed out by @ref per_draw_ring::allocate: the shared group
    // to bind at the material's per-draw slot and the dynamic offset of the
    // slot's block within the group's buffer. An invalid group means no
    // slot could be had and the draw is skipped.
    struct per_draw_allocation
    {
        gpu::bind_group bind_group{};
        uint32_t offset{0};
    };

    /**
     * @brief Per-frame linear allocator for the PerDraw blocks.
     *
     * One uniform buffer is split into @ref frames_in_flight regions of
     * @ref slots_per_frame slots each; a slot is one block rounded up to
     * the device's @c uniform_buffer_offset_alignment. @ref begin_frame
     * moves to the device's current frame slot's region and rewinds its
     * cursor, @ref allocate hands out the region's slots in order. Nothing
     * is freed per draw.
     *
     * Synchronisation. The host writes a region while the GPU may still
     * be reading the ones it wrote the frames before, so a region must not
     * be reused until the frame that read it has retired. The buffer holds
     * one region per frame the device keeps in flight
     * (@c device::frames_in_flight) and a frame writes the region of its
     * @c device::frame_slot: the Vulkan backend waits that slot's fence in
     * @c device::begin_frame before the renderer records anything (the
     * frame-top wait of #189), so the region a frame rewrites is one the
     * GPU is done with; OpenGL keeps one frame in flight and orders
     * @c glBufferSubData against earlier draws itself. Because the ring
     * partitions the buffer per slot itself, it is created with the
     * @c stream_data hint and the backend keeps a single copy of it
     * rather than one per slot as it does for @c dynamic_data buffers.
     *
     * Overflow. A frame that draws more than a region holds spills into
     * extra buffers of the same slot count for the rest of that frame
     * (each with its own groups), so every draw still gets a slot of its
     * own; the next @ref begin_frame releases them and regrows the main
     * buffer to fit what the frame used plus headroom. A slot is never
     * handed out twice in a frame, and a buffer the GPU may still read is
     * only ever released through @c device::destroy, which the Vulkan
     * backend defers until that frame has retired.
     *
     * Groups. A group is created the first time a layout draws from a
     * buffer and shared by every later draw under that layout; one that
     * has gone unused for @ref idle_frames_before_release frames (its
     * material was destroyed, say) is released, so layouts that come and
     * go do not pile up descriptor sets.
     *
     * Owned by @ref renderer (created in @c init after the device, released
     * in @c quit before it) and reached by the renderables through
     * @ref per_draw_binding. Main-thread only.
     */
    class per_draw_ring
    {
    public:
        // Slots per region before the first growth.
        static constexpr uint32_t initial_slots_per_frame = 1024;

        // Frames a shared group may go unused before it is released.
        static constexpr uint64_t idle_frames_before_release = 256;

        explicit per_draw_ring(gpu::device& device);
        ~per_draw_ring();

        per_draw_ring(const per_draw_ring&) = delete;
        per_draw_ring& operator=(const per_draw_ring&) = delete;

        // Open a frame: release last frame's spill buffers (growing the
        // main buffer if it spilled), move to the next region and rewind.
        // Called by @c renderer::render right after @c device::begin_frame,
        // before any pass collects.
        void begin_frame();

        // Copy @p payload into the next free slot of this frame and return
        // the group over that slot's buffer for @p layout (created on first
        // use, then shared) with the slot's offset. Invalid when @p layout
        // is invalid or no buffer or group could be created.
        per_draw_allocation allocate(gpu::bind_group_layout layout, const per_draw_payload& payload);

        // Increases with every @ref begin_frame; an allocation is valid for
        // the frame whose serial it was made under. Never 0, so a zero
        // serial marks "nothing allocated yet".
        uint64_t frame_serial() const noexcept
        {
            return m_frame_serial;
        }

        // Current capacity of one region, in slots.
        uint32_t slots_per_frame() const noexcept
        {
            return m_slots_per_frame;
        }

        // Regions the buffer is split into: the device's frames in
        // flight, read once at construction. See the class comment.
        uint32_t frames_in_flight() const noexcept
        {
            return m_frames_in_flight;
        }

        // Whether the device takes the PerDraw block as push constants
        // (@ref per_draw_push_constants), read once at construction.
        // Rigid draws then push their block rather than take a slot, and
        // the main buffer is only created if something allocates anyway.
        bool uses_push_constants() const noexcept
        {
            return m_push_constants;
        }

    private:
        // A buffer blocks are written into, with the groups built over it
        // (one per per-draw layout that has drawn from it).
        struct chunk
        {
            struct layout_group
            {
                gpu::bind_group_layout layout{};
                gpu::bind_group group{};
                // Frame serial of the last allocation through the group.
                uint64_t last_used{0};
            };

            gpu::buffer buffer{};
            std::vector<layout_group> groups;
        };

        gpu::buffer create_buffer(uint32_t slots, const char* name);
        void release(chunk& target);
        gpu::bind_group group_for(chunk& target, gpu::bind_group_layout layout);
        chunk* spill_chunk();

        gpu::device& m_device;

        // One block rounded up to the offset alignment.
        uint32_t m_stride{0};
        uint32_t m_slots_per_frame{0};
        uint32_t m_frames_in_flight{1};
        bool m_push_constants{false};

        // The main buffer: m_frames_in_flight regions of m_slots_per_frame.
        // m_region is the device's frame slot, taken at begin_frame.
        chunk m_main;
        uint32_t m_region{0};
        uint32_t m_cursor{0};

        // This frame's overflow buffers, each m_slots_per_frame slots, and
        // the cursor into the last one.
        std::vector<chunk> m_spill;
        uint32_t m_spill_cursor{0};

        // Slots handed out this frame, main and spill together.
        uint32_t m_used{0};

        uint64_t m_frame_serial{1};

        // Set once a failed buffer creation has been reported.
        bool m_failure_reported{false};
    };

    /**
     * @brief A renderable's view of its PerDraw block.
     *
     * Holds the block built from the renderable's transform, recomputed —
     * model matrix, normal matrix, mirror test — only when the transform's
     * world version moves.
     *
     * On a device with push constants @ref bind points the draw item at
     * the cached block and the pass pushes it, so a static object costs
     * no matrix work and no buffer write at all.
     *
     * Otherwise it also holds this frame's slot in the @ref per_draw_ring.
     * The first @ref bind of a frame copies the block into a fresh slot;
     * every later pass the renderable draws in that frame (the shadow
     * passes collect before the scene pass) binds the same slot, so they
     * all read the same offset and nothing is written twice. A static
     * object therefore costs one 128-byte copy per frame and no matrix
     * work.
     */
    class per_draw_binding
    {
    public:
        // Rebuild the cached block from @p transform if its world matrix
        // changed since the last call. Returns true when it did.
        bool refresh(const util::transform& transform);

        // Refresh, then point @p item at the block and set its mirror
        // flag: on a device with push constants at the cached block the
        // pass pushes (@ref draw_item::per_draw_push), otherwise at this
        // frame's copy of it, the shared group for @p layout plus the
        // dynamic offset. False when no slot could be had; the caller
        // skips the draw.
        bool bind(const util::transform& transform, gpu::bind_group_layout layout, draw_item& item);

        // The cached block and its mirror flag, as of the last refresh.
        // The block keeps its address for the binding's lifetime.
        const per_draw_payload& payload() const noexcept
        {
            return m_payload;
        }

        bool mirrored() const noexcept
        {
            return m_mirrored;
        }

        // The transform world version the cached block was built from
        // (0 before the first refresh).
        uint64_t world_version() const noexcept
        {
            return m_world_version;
        }

    private:
        per_draw_payload m_payload{};
        bool m_mirrored{false};
        uint64_t m_world_version{0};

        // This frame's slot: valid while m_frame is the ring's serial and
        // the layout is unchanged.
        per_draw_allocation m_allocation{};
        gpu::bind_group_layout m_layout{};
        uint64_t m_frame{0};
    };
} // namespace rendering_engine
