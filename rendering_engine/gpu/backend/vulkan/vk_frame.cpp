// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_frame.cpp
 * @brief @c vk_frame: the frame slots, their fences and command pools,
 *        the queue submissions of the frame's work and the deferred
 *        destroys they gate.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_frame.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_physical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_transfer.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    vk_frame::vk_frame(const vk_physical_device& physical_device,
                       vk_logical_device& device,
                       const vk_transfer& transfer)
        : m_physical_device{physical_device}, m_device{device}, m_transfer{transfer}
    {
    }

    void vk_frame::set_frames_in_flight(uint32_t frames_in_flight)
    {
        // The slot ring is sized once, at init: the command pools, sync
        // objects, swapchain depth images and every dynamic buffer's
        // regions follow it, so it cannot change while the device is
        // up. The settings layer already clamps the value to the range;
        // the clamp here guards any other caller.
        static_assert(gpu::max_frames_in_flight == k_max_frames_in_flight,
                      "the device interface's bound and the backend ring must agree");
        m_frames_in_flight = std::clamp<uint32_t>(frames_in_flight, 1, k_max_frames_in_flight);
        m_frame_slot = 0;
        m_in_frame = false;
        m_submit_serial = 0;
        m_completed_submit_serial = 0;
    }

    void vk_frame::create_command_pools()
    {
        // A frame pool is reset whole, which is the cheaper operation
        // and needs no per-buffer flag. Transient: every buffer is
        // recorded once and reset.
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = m_physical_device.graphics_queue_family();
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        for (uint32_t slot = 0; slot < m_frames_in_flight; ++slot)
        {
            frame_command_slot& frame_slot = m_frame_command_slots[slot];
            if (!vk_check(vkCreateCommandPool(m_device.handle(), &info, nullptr, &frame_slot.pool),
                          "vkCreateCommandPool (frame)"))
            {
                frame_slot.pool = VK_NULL_HANDLE;
                throw std::runtime_error{"vkCreateCommandPool failed"};
            }
        }
    }

    void vk_frame::destroy_command_pools()
    {
        // Under vkDeviceWaitIdle (quit): destroying the pools frees
        // their command buffers.
        for (frame_command_slot& slot : m_frame_command_slots)
        {
            slot.buffers.clear();
            slot.next = 0;
            if (slot.pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(m_device.handle(), slot.pool, nullptr);
                slot.pool = VK_NULL_HANDLE;
            }
            for (frame_command_slot::lane& lane : slot.lanes)
            {
                if (lane.pool != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(m_device.handle(), lane.pool, nullptr);
                }
            }
            slot.lanes.clear();
        }
    }

    void vk_frame::create_sync_objects()
    {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        // The render-finished semaphores live with the swapchain (one per
        // image); the per-slot image-available semaphores and in-flight
        // fences are owned here.
        for (uint32_t slot = 0; slot < m_frames_in_flight; ++slot)
        {
            if (!vk_check(vkCreateSemaphore(m_device.handle(), &si, nullptr, &m_image_available[slot]),
                          "vkCreateSemaphore (image-available)") ||
                !vk_check(vkCreateFence(m_device.handle(), &fi, nullptr, &m_in_flight_fences[slot]),
                          "vkCreateFence (in-flight)"))
            {
                throw std::runtime_error{"vk sync objects"};
            }
        }
        m_in_flight_fence_armed.fill(false);
        m_fence_submit_serial.fill(0);
    }

    void vk_frame::destroy_sync_objects()
    {
        if (m_device.handle() == VK_NULL_HANDLE)
        {
            return;
        }
        for (uint32_t slot = 0; slot < k_max_frames_in_flight; ++slot)
        {
            if (m_image_available[slot] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_device.handle(), m_image_available[slot], nullptr);
                m_image_available[slot] = VK_NULL_HANDLE;
            }
            if (m_in_flight_fences[slot] != VK_NULL_HANDLE)
            {
                vkDestroyFence(m_device.handle(), m_in_flight_fences[slot], nullptr);
                m_in_flight_fences[slot] = VK_NULL_HANDLE;
            }
        }
        m_in_flight_fence_armed.fill(false);
    }

    void vk_frame::reset()
    {
        m_in_flight_fence_armed.fill(false);
        m_fence_submit_serial.fill(0);
        m_submit_serial = 0;
        m_completed_submit_serial = 0;
        m_frame_slot = 0;
        m_frames_in_flight = 1;
        m_in_frame = false;
    }

    bool vk_frame::begin()
    {
        m_in_frame = true;
        // Block until the command buffer of the frame that last used
        // this slot — frames_in_flight frames ago — has finished
        // executing. This runs before the renderer records anything for
        // the new frame, so every host write that follows — this slot's
        // regions of the per-frame camera / light / shadow UBOs, instance
        // re-uploads — lands in memory the GPU is no longer reading.
        // Waiting lazily at the first swapchain pass instead, after every
        // off-screen pass had already written its UBOs, would race those
        // host writes against the GPU's reads. The fence is only waited
        // when a submission armed it: after a failed submit nothing would
        // ever signal it.
        return wait_slot_fence(m_frame_slot);
    }

    void vk_frame::end()
    {
        if (m_frame_index < k_diagnostic_frames)
        {
            LOG_INF("Vulkan frame %u: passes(off=%u, swap=%u) draws(non_indexed=%u, indexed=%u) verts=%u idxs=%u",
                    m_frame_index,
                    m_frame_stats.passes_offscreen,
                    m_frame_stats.passes_swapchain,
                    m_frame_stats.draws,
                    m_frame_stats.draws_indexed,
                    m_frame_stats.vertices,
                    m_frame_stats.indices);
        }
        m_frame_stats = {};
        ++m_frame_index;
        // The next frame records into the next slot; its begin_frame
        // waits that slot's fence.
        m_frame_slot = (m_frame_slot + 1) % m_frames_in_flight;
        m_in_frame = false;
    }

    void vk_frame::abandon_frame() noexcept
    {
        m_in_frame = false;
    }

    void vk_frame::submit_without_image(VkCommandBuffer cmd)
    {
        // No swapchain image this frame: either work submitted
        // outside a frame bracket (the IBL prefilter at start-up)
        // or a frame whose passes never reached the swapchain. It
        // runs on the current slot's fence like a frame submission,
        // with no semaphores and no present: the next begin_frame
        // of that slot waits for it before the pool reset and the
        // deferred-destroy drain, so the command buffer is not
        // reused and nothing it references (the IBL scaffold is
        // destroyed right after its submit) is freed while it
        // executes. A fence still armed by an earlier such
        // submission is waited first — for that one submission, not
        // the whole queue.
        const uint32_t slot = m_frame_slot;
        VkFence fence = m_in_flight_fences[slot];
        if (!wait_slot_fence(slot))
        {
            return;
        }
        if (!vk_check(vkResetFences(m_device.handle(), 1, &fence), "vkResetFences (no-image)"))
        {
            return;
        }
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        if (m_device.check_queue_result(vkQueueSubmit(m_device.graphics_queue(), 1, &si, fence),
                                        "vkQueueSubmit (no-image)"))
        {
            m_in_flight_fence_armed[slot] = true;
            m_fence_submit_serial[slot] = ++m_submit_serial;
        }
    }

    bool vk_frame::submit_with_image(VkCommandBuffer cmd, VkSemaphore render_finished)
    {
        const uint32_t slot = m_frame_slot;
        VkFence fence = m_in_flight_fences[slot];
        // begin_frame already waited this slot's fence for the frame
        // that last used it, so it is signaled and idle unless a
        // no-image submission armed it again this frame. Reset it
        // here, right before the one submission that signals it again,
        // rather than at acquire time: a reset at acquire time would
        // leave the fence unsignaled whenever the acquire fails
        // (out-of-date swapchain), and the next begin_frame would then
        // block forever.
        if (!wait_slot_fence(slot))
        {
            return false;
        }
        if (!vk_check(vkResetFences(m_device.handle(), 1, &fence), "vkResetFences"))
        {
            return false;
        }

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &m_image_available[slot];
        si.pWaitDstStageMask = &wait_stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &render_finished;
        if (!m_device.check_queue_result(vkQueueSubmit(m_device.graphics_queue(), 1, &si, fence), "vkQueueSubmit"))
        {
            // Nothing signals the fence or the render-finished
            // semaphore now: the fence stays disarmed so the next
            // begin_frame does not wait on it forever.
            return false;
        }
        m_in_flight_fence_armed[slot] = true;
        m_fence_submit_serial[slot] = ++m_submit_serial;
        return true;
    }

    bool vk_frame::wait_slot_fence(uint32_t slot)
    {
        if (slot >= k_max_frames_in_flight || !m_in_flight_fence_armed[slot])
        {
            return true;
        }
        // Disarmed before the wait: after a failure nothing would ever
        // signal it, and a lost device is done anyway.
        m_in_flight_fence_armed[slot] = false;
        if (!m_device.check_queue_result(
                vkWaitForFences(m_device.handle(), 1, &m_in_flight_fences[slot], VK_TRUE, UINT64_MAX),
                "vkWaitForFences"))
        {
            return false;
        }
        // The queue completes submissions in order, so this one
        // retiring proves every earlier one retired too.
        m_completed_submit_serial = std::max(m_completed_submit_serial, m_fence_submit_serial[slot]);
        return true;
    }

    void vk_frame::note_device_idle()
    {
        m_in_flight_fence_armed.fill(false);
        m_completed_submit_serial = std::max(m_completed_submit_serial, m_submit_serial);
    }

    void vk_frame::note_device_drained()
    {
        note_device_idle();
        m_completed_submit_serial = UINT64_MAX;
    }

    void vk_frame::wait_slot_before_host_write()
    {
        if (!m_in_frame)
        {
            // Between frames the current slot's region still belongs to
            // the frame that last recorded into the slot until its fence
            // retires — the wait begin_frame would do next, brought
            // forward. It disarms the fence, so the frame's own wait is
            // then free.
            wait_slot_fence(m_frame_slot);
        }
    }

    void vk_frame::reset_frame_command_pool()
    {
        frame_command_slot& slot = m_frame_command_slots[m_frame_slot];
        if (slot.pool != VK_NULL_HANDLE)
        {
            vk_check(vkResetCommandPool(m_device.handle(), slot.pool, 0), "vkResetCommandPool (frame)");
        }
        slot.next = 0;
        // The secondaries of this slot's frame were executed by its
        // primary, so the same fence wait proved them complete.
        for (frame_command_slot::lane& lane : slot.lanes)
        {
            if (lane.pool != VK_NULL_HANDLE)
            {
                vk_check(vkResetCommandPool(m_device.handle(), lane.pool, 0), "vkResetCommandPool (lane)");
            }
            lane.next = 0;
        }
    }

    VkCommandBuffer vk_frame::acquire_secondary_command_buffer(uint32_t lane_index)
    {
        if (m_device.device_lost() || m_device.handle() == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        frame_command_slot& slot = m_frame_command_slots[m_frame_slot];
        // Lanes are appended as a wider fork asks for them; a pool that
        // failed to create leaves its lane empty and every later request
        // for it retries.
        if (lane_index >= slot.lanes.size())
        {
            slot.lanes.resize(static_cast<size_t>(lane_index) + 1);
        }
        frame_command_slot::lane& lane = slot.lanes[lane_index];
        if (lane.pool == VK_NULL_HANDLE)
        {
            // Transient and reset whole, like the frame's primary pool.
            VkCommandPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.queueFamilyIndex = m_physical_device.graphics_queue_family();
            info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (!vk_check(vkCreateCommandPool(m_device.handle(), &info, nullptr, &lane.pool),
                          "vkCreateCommandPool (lane)"))
            {
                lane.pool = VK_NULL_HANDLE;
                return VK_NULL_HANDLE;
            }
        }
        if (lane.next < lane.buffers.size())
        {
            return lane.buffers[lane.next++];
        }
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = lane.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (!vk_check(vkAllocateCommandBuffers(m_device.handle(), &ai, &cmd), "vkAllocateCommandBuffers (lane)"))
        {
            return VK_NULL_HANDLE;
        }
        lane.buffers.push_back(cmd);
        ++lane.next;
        return cmd;
    }

    VkCommandBuffer vk_frame::acquire_frame_command_buffer()
    {
        if (m_device.device_lost())
        {
            return VK_NULL_HANDLE;
        }
        frame_command_slot& slot = m_frame_command_slots[m_frame_slot];
        if (slot.pool == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        if (slot.next < slot.buffers.size())
        {
            return slot.buffers[slot.next++];
        }
        // The slot has handed out every buffer it owns since the last
        // reset (one per frame in the steady state, more only when
        // several encoders are recorded between two frames); grow it.
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = slot.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (!vk_check(vkAllocateCommandBuffers(m_device.handle(), &ai, &cmd), "vkAllocateCommandBuffers (frame)"))
        {
            return VK_NULL_HANDLE;
        }
        slot.buffers.push_back(cmd);
        ++slot.next;
        return cmd;
    }

    void vk_frame::enqueue_destroy(std::function<void()> fn)
    {
        if (fn)
        {
            // Inside a frame the frame's own submission, still to come,
            // may reference the resource; between frames only the
            // submissions already queued can. The newest batch — open,
            // or submitted and maybe still executing — is the last one
            // that can hold a copy into the resource; a batch begun
            // later never sees its handle.
            const uint64_t submit_serial = m_in_frame ? m_submit_serial + 1 : m_submit_serial;
            m_pending_destroys.push_back({submit_serial, m_transfer.newest_transfer_batch_id(), std::move(fn)});
        }
    }

    void vk_frame::drain_pending_destroys()
    {
        // Move out first so a destroy callback that itself enqueues
        // is captured into the next drain rather than running here.
        // An entry whose submission or transfer batch has not retired
        // yet goes back in the queue for a later drain.
        std::vector<pending_destroy> drain;
        drain.swap(m_pending_destroys);
        for (pending_destroy& entry : drain)
        {
            if (entry.submit_serial > m_completed_submit_serial ||
                m_transfer.transfer_batch_live_up_to(entry.transfer_batch_id))
            {
                m_pending_destroys.push_back(std::move(entry));
                continue;
            }
            entry.fn();
        }
    }

    void vk_frame::note_render_pass_opened(bool is_swapchain, bool use_depth)
    {
        if (is_swapchain)
        {
            ++m_frame_stats.passes_swapchain;
        }
        else
        {
            ++m_frame_stats.passes_offscreen;
        }
        (void)use_depth;
    }

    void vk_frame::note_draw(uint32_t vertex_count)
    {
        ++m_frame_stats.draws;
        m_frame_stats.vertices += vertex_count;
    }

    void vk_frame::note_draw_indexed(uint32_t index_count)
    {
        ++m_frame_stats.draws_indexed;
        m_frame_stats.indices += index_count;
    }

    void vk_frame::note_draws(uint32_t draws, uint32_t vertices, uint32_t draws_indexed, uint32_t indices)
    {
        m_frame_stats.draws += draws;
        m_frame_stats.vertices += vertices;
        m_frame_stats.draws_indexed += draws_indexed;
        m_frame_stats.indices += indices;
    }

    uint32_t vk_frame::frames_in_flight() const noexcept
    {
        return m_frames_in_flight;
    }
    uint32_t vk_frame::frame_slot() const noexcept
    {
        return m_frame_slot;
    }
    VkSemaphore vk_frame::image_available() const noexcept
    {
        return m_image_available[m_frame_slot];
    }
} // namespace rendering_engine::gpu::backend::vulkan
