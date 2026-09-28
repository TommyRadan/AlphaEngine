// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_frame.hpp
 * @brief The frames in flight: the per-slot sync objects and command
 *        pools, the submission serials their fences retire, and the
 *        deferred-destroy queue gated on them.
 *
 * The backend keeps up to k_max_frames_in_flight frames in flight
 * (rendering_engine::graphics_settings::frames_in_flight, 2 by
 * default): each frame records into its own slot — an image-available
 * semaphore, an in-flight fence and a command pool (the swapchain keeps
 * a depth image per slot too) — and the frame boundary waits only for
 * the frame that last used the slot, so the CPU records frame N+1 while
 * the GPU draws frame N.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_logical_device;
    class vk_physical_device;
    class vk_transfer;

    class vk_frame
    {
    public:
        vk_frame(const vk_physical_device& physical_device, vk_logical_device& device, const vk_transfer& transfer);

        // Size the slot ring, once, at init: the command pools, sync
        // objects, swapchain depth images and every dynamic buffer's
        // regions follow it, so it cannot change while the device is
        // up. @p frames_in_flight is clamped to k_max_frames_in_flight.
        void set_frames_in_flight(uint32_t frames_in_flight);
        // The per-slot pools the frame encoders draw on. Throws when a
        // pool cannot be created.
        void create_command_pools();
        // Destroy the slot pools and every lane's pool, which frees
        // their command buffers (quit, under vkDeviceWaitIdle).
        void destroy_command_pools();
        void create_sync_objects();
        void destroy_sync_objects();
        // Forget the ring and the serials (quit, for the next init).
        void reset();

        // The slot ring: the count set_frames_in_flight settled on, and
        // the current frame's slot, which advances at end.
        uint32_t frames_in_flight() const noexcept;
        uint32_t frame_slot() const noexcept;
        // The semaphore the current slot's acquire signals and its
        // submission waits.
        VkSemaphore image_available() const noexcept;

        // Enter the frame and wait the in-flight fence of its slot —
        // armed by the frame that last recorded into it,
        // frames_in_flight frames ago. Returns false when the wait
        // failed (the device is then lost).
        bool begin();
        // Log the frame's draw counters (for the first few frames),
        // clear them, and advance to the next slot.
        void end();
        // Leave the frame without ending it (quit): whatever it
        // recorded will never be submitted.
        void abandon_frame() noexcept;

        // Queue @p cmd, a frame's command buffer that reached no
        // swapchain image, on the current slot's fence alone.
        void submit_without_image(VkCommandBuffer cmd);
        // Queue @p cmd, which renders into the acquired swapchain image:
        // it waits the slot's image-available semaphore and signals
        // @p render_finished (that image's) and the slot's fence.
        // Returns false when nothing was queued.
        bool submit_with_image(VkCommandBuffer cmd, VkSemaphore render_finished);

        // Wait the in-flight fence of @p slot when a submission armed
        // it, disarm it, and mark the submission it covers (and every
        // earlier one) retired for the deferred destroys. Returns false
        // when the wait failed (the device is then lost).
        bool wait_slot_fence(uint32_t slot);
        // After vkDeviceWaitIdle: every fence is idle and every
        // submission has retired.
        void note_device_idle();
        // After vkDeviceWaitIdle with nothing left to submit (quit, a
        // flush of the deferred destroys): every submission counts as
        // retired, even one stamped but never made.
        void note_device_drained();
        // Before the host writes a multi-buffered region outside a
        // frame: the frame that last used the current slot may still
        // read it, so its fence is waited (once; the wait disarms it).
        void wait_slot_before_host_write();
        // vkResetCommandPool on the current frame's pool, after the
        // fence wait proved every buffer from it complete, and rewind
        // its hand-out cursor.
        void reset_frame_command_pool();

        // A primary command buffer for a command encoder, from the
        // current frame's pool. Buffers are handed out in order and
        // reclaimed together when begin_frame resets the pool after the
        // fence wait, so nothing is allocated or freed per frame in the
        // steady state. Returns VK_NULL_HANDLE when the device is lost
        // or the allocation failed (logged).
        VkCommandBuffer acquire_frame_command_buffer();

        // A secondary command buffer for a parallel render pass's chunk,
        // from recording lane @p lane's pool of the current frame slot
        // (the pool and the lane itself are created on first use). Like
        // the primary buffers, lanes hand their buffers out in order and
        // take them all back with the pool reset at the slot's next
        // begin_frame. Main thread only, before the fork: the lane's
        // pool is then the recording thread's alone until the join.
        // Returns VK_NULL_HANDLE when the device is lost or the pool or
        // buffer could not be created (logged).
        VkCommandBuffer acquire_secondary_command_buffer(uint32_t lane);

        // Destroy callbacks queued from vk_device's @c destroy()
        // overloads. Freeing a buffer or descriptor set while a command
        // buffer that references it is executing, or still being
        // recorded, is invalid (VUID-vkDestroyBuffer-buffer-00922 /
        // VUID-vkFreeDescriptorSets-pDescriptorSets-00309) — the
        // engine's buffer / bind-group churn would otherwise
        // trigger streams of it — and with several frames in flight
        // the previous frame's command buffer is still running when a
        // resource is destroyed. Each
        // @c destroy() pushes a closure here stamped with the serial of
        // the last queue submission that can reference the resource:
        // inside a frame that is the frame's own submission, still to
        // come; outside a frame the most recent one, since nothing is
        // being recorded. Every frame submission arms its slot's fence
        // with its serial, and waiting a fence marks its serial — and
        // every earlier one, which the queue completed first — retired.
        // @c drain_pending_destroys runs the entries whose serial has
        // retired, and only at points where no open command buffer can
        // reference them: in vk_device::begin_frame, after the slot's
        // fence wait and before the renderer records anything (so a
        // bind group a material rebuilds mid-frame is never freed while
        // the open command buffer already references it); under
        // vkDeviceWaitIdle in vk_device::quit; and under
        // vk_device::flush_pending_destroys for a caller with the same
        // problem outside this queue (the ImGui Vulkan backend's own
        // descriptor sets — see vk_overlay_renderer.cpp). A transfer
        // batch may reference the resource as well — a copy into a
        // buffer or image destroyed before the batch ran — so each entry
        // also records the newest batch id at enqueue time and runs only
        // once every batch up to that id has retired.
        void enqueue_destroy(std::function<void()> fn);
        void drain_pending_destroys();

        // Per-frame draw counters surfaced as a one-shot log for the
        // first few frames so a missing draw call is visible without
        // attaching RenderDoc. Cleared in end_frame. Main thread only:
        // a secondary encoder tallies its own draws and the primary
        // merges them through note_draws when it executes the secondary.
        void note_render_pass_opened(bool is_swapchain, bool use_depth);
        void note_draw(uint32_t vertex_count);
        void note_draw_indexed(uint32_t index_count);
        void note_draws(uint32_t draws, uint32_t vertices, uint32_t draws_indexed, uint32_t indices);

    private:
        const vk_physical_device& m_physical_device;
        vk_logical_device& m_device;
        const vk_transfer& m_transfer;

        // Frame command buffers. Each slot is a command pool plus the
        // primary buffers allocated from it so far, handed out in order
        // by acquire_frame_command_buffer and reclaimed together by a
        // pool reset at begin_frame once the slot's fence wait has
        // proved them complete. m_frames_in_flight slots are in use;
        // m_frame_slot is the current frame's and advances at
        // end_frame.
        struct frame_command_slot
        {
            VkCommandPool pool{VK_NULL_HANDLE};
            std::vector<VkCommandBuffer> buffers;
            size_t next{0};

            // The secondary buffers of a parallel render pass, one pool
            // per recording lane (see acquire_secondary_command_buffer)
            // so the thread recording a lane's chunk never shares a pool
            // with another; handed out and reset exactly like the
            // primary buffers above. Lanes are appended on first use and
            // kept until quit.
            struct lane
            {
                VkCommandPool pool{VK_NULL_HANDLE};
                std::vector<VkCommandBuffer> buffers;
                size_t next{0};
            };
            std::vector<lane> lanes;
        };
        std::array<frame_command_slot, k_max_frames_in_flight> m_frame_command_slots{};

        uint32_t m_frames_in_flight{1};
        uint32_t m_frame_slot{0};
        // Between begin_frame and end_frame. Decides which submission a
        // deferred destroy waits for and whether a host write to a
        // multi-buffered region must wait the slot's fence itself.
        bool m_in_frame{false};

        // Per frame slot: the semaphore its acquire signals and its
        // submission waits, and the fence its submission signals.
        // begin_frame waits the slot's fence before the frame records
        // anything and submit resets it right before the submission
        // that signals it; the fences are created signaled so the first
        // lap does not block. A fence is armed only by a submission
        // that succeeded, so a failed vkQueueSubmit (nothing will ever
        // signal the fence) does not leave a begin_frame waiting
        // forever; the serial it was armed with is what the wait
        // retires for the deferred destroys.
        std::array<VkSemaphore, k_max_frames_in_flight> m_image_available{};
        std::array<VkFence, k_max_frames_in_flight> m_in_flight_fences{};
        std::array<bool, k_max_frames_in_flight> m_in_flight_fence_armed{};
        std::array<uint64_t, k_max_frames_in_flight> m_fence_submit_serial{};
        // Queue submissions of frame command buffers so far, and the
        // highest serial a fence wait (or an idle wait) has proved
        // complete; see enqueue_destroy.
        uint64_t m_submit_serial{0};
        uint64_t m_completed_submit_serial{0};

        // See enqueue_destroy: the closure, the frame submission it
        // waits for, and the newest transfer batch id at the time it
        // was queued (0 when none had begun).
        struct pending_destroy
        {
            uint64_t submit_serial{0};
            uint64_t transfer_batch_id{0};
            std::function<void()> fn;
        };
        std::vector<pending_destroy> m_pending_destroys;

        // Frame-level diagnostic counters. Logged at end_frame() for
        // @c k_diagnostic_frames frames after init so the user can
        // see whether scene_pass actually issued the cube draw.
        struct frame_stats
        {
            uint32_t passes_offscreen{0};
            uint32_t passes_swapchain{0};
            uint32_t draws{0};
            uint32_t draws_indexed{0};
            uint32_t vertices{0};
            uint32_t indices{0};
        };
        frame_stats m_frame_stats{};
        uint32_t m_frame_index{0};
        static constexpr uint32_t k_diagnostic_frames = 3;
    };
} // namespace rendering_engine::gpu::backend::vulkan
