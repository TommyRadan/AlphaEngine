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
     * moves to the next region and rewinds its cursor, @ref allocate hands
     * out the region's slots in order. Nothing is freed per draw.
     *
     * Synchronisation. The host writes a region while the GPU may still
     * be reading the one it wrote the frame before, so a region must not
     * be reused until the frame that read it has retired. Today there is
     * one region, and that is safe only because the Vulkan backend keeps a
     * single frame in flight and @c device::begin_frame waits for the
     * previous frame's fence before the renderer records anything (the
     * frame-top wait of #189); OpenGL's @c glBufferSubData is ordered
     * against earlier draws by the driver. Raising the number of frames in
     * flight (#169) must raise @ref frames_in_flight to match, so each
     * frame writes a region the GPU is done with; the offsets already
     * account for the region index.
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
     * Owned by @ref context (created in @c init after the device, released
     * in @c quit before it) and reached by the renderables through
     * @ref per_draw_binding. Main-thread only.
     */
    class per_draw_ring
    {
    public:
        // Regions the buffer is split into; see the class comment. Must
        // match the backend's frames in flight.
        static constexpr uint32_t frames_in_flight = 1;

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
        // Called by @c context::render right after @c device::begin_frame,
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

        // The main buffer: frames_in_flight regions of m_slots_per_frame.
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
     * world version moves, and this frame's slot in the @ref per_draw_ring.
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

        // Refresh, then point @p item at this frame's copy of the block:
        // the shared group for @p layout, the dynamic offset and the
        // mirror flag. False when no slot could be had; the caller skips
        // the draw.
        bool bind(const util::transform& transform, gpu::bind_group_layout layout, draw_item& item);

        // The cached block and its mirror flag, as of the last refresh.
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
