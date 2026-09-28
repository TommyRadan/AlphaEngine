// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_swapchain.hpp
 * @brief The swapchain: its images and the per-slot depth buffers and
 *        per-image semaphores built with it, the acquire and present of
 *        each frame, and the suspension while there is nothing to
 *        present into.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/vulkan/vk_allocator.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_frame;
    class vk_instance;
    class vk_logical_device;
    class vk_physical_device;

    // A rebuild also retires everything built on the old images, which
    // vk_device owns, so the swapchain is handed its owner's rebuild
    // (vk_device::recreate_swapchain) for the out-of-date acquire and
    // present to call.
    class vk_swapchain
    {
    public:
        vk_swapchain(const vk_instance& instance,
                     const vk_physical_device& physical_device,
                     vk_logical_device& device,
                     vk_frame& frame,
                     std::function<bool()> recreate);

        // The drawable size the engine last reported, in pixels, and
        // whether presentation waits for vertical sync.
        void set_window_size(uint32_t width, uint32_t height);
        void set_vsync(bool vsync);
        // The extent the next swapchain has to be built with for a
        // surface with @p caps (see choose_swapchain_extent).
        VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps) const;

        // Build the swapchain for @p extent plus everything hanging off
        // it (image views, one depth buffer per frame slot, the
        // per-image render-finished semaphores). The previous swapchain, if any,
        // is handed over as oldSwapchain — which retires it whether or
        // not the call succeeds — and released here. Returns false,
        // with nothing left half-built, when any step fails; the caller
        // decides whether that is fatal (init) or a suspension (the
        // render loop).
        bool create_swapchain(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D extent);
        void destroy_swapchain();
        // Enter the suspended state, logging @p reason once per
        // suspension (at error level when @p is_error).
        void suspend_swapchain(const char* reason, bool is_error);
        // Leave the suspended state after a rebuild succeeded, logging
        // the resumption once.
        void resume();
        // Forget the frame's image and the suspension (quit, for the
        // next init).
        void reset();

        // Acquire the next swapchain image for the current frame.
        // Called lazily by the render-pass encoder when the first
        // swapchain-targeted pass opens, so a frame that never reaches
        // the swapchain does not acquire one. One attempt per frame:
        // the frame's later swapchain passes see the same outcome, so
        // a frame either reaches the swapchain in every pass or in
        // none. Must run inside a begin_frame / end_frame bracket.
        // An out-of-date acquire rebuilds the swapchain against the
        // surface's current extent and retries once, so the frame
        // lands in the new swapchain; a rebuild that finds no usable
        // extent (minimised) suspends instead and the frame skips the
        // swapchain. Because a rebuild retires the swapchain target's
        // render-pass variants, callers read nothing from that target
        // until this returns. The slot's fence is not touched here —
        // submit resets it right before the queue submission that
        // signals it, so a failed acquire never leaves it unsignaled
        // for the next begin_frame to block on. An image handed back
        // while the frame that last drew it is still in flight waits
        // that frame's fence (see m_image_last_slot).
        void acquire_swapchain_image();
        // The frame's command buffer was queued against the acquired
        // image from frame slot @p slot: the frame owns the image until
        // its fence retires, and present issues it.
        void note_submitted(uint32_t slot);
        // Present the image acquired this frame when its command buffer
        // was queued, rebuild when the present reports the swapchain
        // out of date or suboptimal, and forget the image either way.
        void present();

        VkSwapchainKHR handle() const noexcept;
        bool suspended() const noexcept;
        VkExtent2D extent() const noexcept;
        VkFormat surface_format() const noexcept;
        const std::vector<VkImageView>& image_views() const noexcept;
        // The depth view of frame slot @p slot.
        VkImageView depth_view(uint32_t slot) const noexcept;
        // The engine-side format of the swapchain depth buffers.
        texture_format depth_format() const noexcept;
        uint32_t image_count() const noexcept;
        // Counter bumped on every successful swapchain (re)build.
        uint64_t generation() const noexcept;
        uint32_t current_image_index() const noexcept;
        bool have_current_image() const noexcept;
        // The render-finished semaphore of the acquired image.
        VkSemaphore render_finished() const noexcept;

    private:
        const vk_instance& m_instance;
        const vk_physical_device& m_physical_device;
        vk_logical_device& m_device;
        vk_frame& m_frame;
        std::function<bool()> m_recreate;

        // Whether presentation waits for vertical sync
        // (surface_desc::vsync), read at every swapchain build.
        bool m_vsync{false};

        VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
        VkSurfaceFormatKHR m_surface_format{};
        VkPresentModeKHR m_present_mode{VK_PRESENT_MODE_FIFO_KHR};
        VkExtent2D m_swapchain_extent{};
        std::vector<VkImage> m_swapchain_images;
        std::vector<VkImageView> m_swapchain_image_views;
        // One depth buffer per frame slot: the swapchain passes of two
        // frames in flight would otherwise write one image with no
        // dependency between them. A swapchain framebuffer pairs an
        // image with a slot's depth (see vk_device::swapchain_framebuffer_index).
        std::array<VkImage, k_max_frames_in_flight> m_swapchain_depth_images{};
        std::array<VmaAllocation, k_max_frames_in_flight> m_swapchain_depth_allocations{};
        std::array<VkImageView, k_max_frames_in_flight> m_swapchain_depth_views{};
        // The engine-side format of the swapchain depth buffer; the
        // VkFormat backing it is vk_format_for(m_swapchain_depth_format)
        // once resolve_depth_formats has run.
        texture_format m_swapchain_depth_format{texture_format::depth32_float};
        // One render-finished semaphore per swapchain image, indexed by the
        // acquired image index. A semaphore tied to a specific image is not
        // re-signaled until that image is re-acquired, which the acquire/fence
        // flow already gates, so the present operation never races a later
        // frame's submit (VUID-vkQueueSubmit-pSignalSemaphores-00067). Created
        // and destroyed alongside the swapchain so it tracks image-count
        // changes on resize.
        std::vector<VkSemaphore> m_render_finished;
        // The frame slot that last rendered into each swapchain image
        // (k_no_slot for none): the images-in-flight guard. An acquire
        // that hands an image back while the frame that last drew it is
        // still in flight waits that frame's fence first, so the image's
        // render-finished semaphore is never re-signaled while pending.
        static constexpr uint32_t k_no_slot = UINT32_MAX;
        std::vector<uint32_t> m_image_last_slot;
        uint32_t m_current_image_index{0};
        bool m_have_current_image{false};
        // Set by the first acquire_swapchain_image of a frame, whatever
        // its outcome, and cleared in end_frame: a frame gets exactly
        // one attempt, so a pass that opens after a failed acquire
        // does not acquire an image the earlier passes never drew to.
        bool m_acquire_attempted{false};
        // Set by submit once this frame's command buffer has been
        // queued against the acquired image; end_frame presents only
        // then, so a frame whose encoder failed to record does not
        // present an image whose render-finished semaphore will never
        // be signaled.
        bool m_present_pending{false};
        // True while there is nothing to present into: the surface
        // reported a 0x0 extent (minimised) or the last rebuild
        // failed. acquire_swapchain_image hands out no image, submit
        // takes the no-image path, end_frame has nothing to present,
        // and begin_frame polls the surface every frame until a rebuild
        // succeeds. Never set by init, which throws instead.
        bool m_swapchain_suspended{false};
        // See generation().
        uint64_t m_swapchain_generation{0};

        // Last drawable size the engine reported through
        // resize_swapchain, in pixels (seeded from the surface_desc
        // size at init). Only
        // consulted when the surface leaves the extent to the
        // application (currentExtent == UINT32_MAX); everywhere else
        // the surface capabilities decide, so a rebuild never trusts
        // a stale cached size.
        uint32_t m_window_width{0};
        uint32_t m_window_height{0};
    };
} // namespace rendering_engine::gpu::backend::vulkan
