// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_swapchain.cpp
 * @brief @c vk_swapchain: swapchain build and teardown, acquire,
 *        present and suspension.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_swapchain.hpp>

#include <algorithm>
#include <array>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_frame.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_instance.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_physical_device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        VkSurfaceFormatKHR pick_surface_format(VkPhysicalDevice gpu, VkSurfaceKHR surface)
        {
            uint32_t count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data());
            for (const auto& f : formats)
            {
                if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    return f;
                }
            }
            return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                   : formats.front();
        }

        VkPresentModeKHR pick_present_mode(VkPhysicalDevice gpu, VkSurfaceKHR surface, bool vsync_enabled)
        {
            uint32_t count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, nullptr);
            std::vector<VkPresentModeKHR> modes(count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, modes.data());

            const auto supports = [&modes](VkPresentModeKHR wanted)
            { return std::find(modes.begin(), modes.end(), wanted) != modes.end(); };

            if (vsync_enabled)
            {
                // FIFO is always available and locks to the refresh rate.
                return VK_PRESENT_MODE_FIFO_KHR;
            }

            // Vsync off: prefer IMMEDIATE (uncapped, may tear); fall back to
            // MAILBOX (low-latency triple buffering) and finally the
            // guaranteed FIFO when neither is exposed.
            if (supports(VK_PRESENT_MODE_IMMEDIATE_KHR))
            {
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            if (supports(VK_PRESENT_MODE_MAILBOX_KHR))
            {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        // The extent the next swapchain has to be built with. The
        // surface dictates it through currentExtent; only when the
        // surface leaves the choice to the application (UINT32_MAX —
        // some Wayland compositors) does the last window size the
        // engine reported count, clamped to what the surface allows.
        // A 0x0 result means the window is minimised: Vulkan rejects
        // a zero-sized swapchain, so the caller must not build one.
        VkExtent2D
        choose_swapchain_extent(const VkSurfaceCapabilitiesKHR& caps, uint32_t window_width, uint32_t window_height)
        {
            if (caps.currentExtent.width != UINT32_MAX)
            {
                return caps.currentExtent;
            }
            VkExtent2D extent{};
            extent.width = std::clamp(window_width, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(window_height, caps.minImageExtent.height, caps.maxImageExtent.height);
            return extent;
        }
    } // namespace

    vk_swapchain::vk_swapchain(const vk_instance& instance,
                               const vk_physical_device& physical_device,
                               vk_logical_device& device,
                               vk_frame& frame,
                               std::function<bool()> recreate)
        : m_instance{instance}, m_physical_device{physical_device}, m_device{device}, m_frame{frame},
          m_recreate{std::move(recreate)}
    {
    }

    void vk_swapchain::set_window_size(uint32_t width, uint32_t height)
    {
        m_window_width = width;
        m_window_height = height;
    }

    void vk_swapchain::set_vsync(bool vsync)
    {
        m_vsync = vsync;
    }

    VkExtent2D vk_swapchain::choose_extent(const VkSurfaceCapabilitiesKHR& caps) const
    {
        return choose_swapchain_extent(caps, m_window_width, m_window_height);
    }

    bool vk_swapchain::create_swapchain(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D extent)
    {
        m_surface_format = pick_surface_format(m_physical_device.handle(), m_instance.surface());
        m_present_mode = pick_present_mode(m_physical_device.handle(), m_instance.surface(), m_vsync);

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        {
            image_count = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = m_instance.surface();
        info.minImageCount = image_count;
        info.imageFormat = m_surface_format.format;
        info.imageColorSpace = m_surface_format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::array<uint32_t, 2> families{m_physical_device.graphics_queue_family(),
                                               m_physical_device.present_queue_family()};
        if (m_physical_device.graphics_queue_family() != m_physical_device.present_queue_family())
        {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = static_cast<uint32_t>(families.size());
            info.pQueueFamilyIndices = families.data();
        }
        else
        {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = m_present_mode;
        info.clipped = VK_TRUE;
        // Hand the previous swapchain over instead of destroying it
        // first: the presentation engine can carry its resources across
        // (no teardown-then-setup hitch, and no window-in-use failure on
        // platforms that refuse a second swapchain on a surface that
        // still has one). The call retires it whether or not it
        // succeeds, so it is released unconditionally right after.
        info.oldSwapchain = m_swapchain;
        VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
        const VkResult create_result = vkCreateSwapchainKHR(m_device.handle(), &info, nullptr, &new_swapchain);
        destroy_swapchain();
        if (create_result != VK_SUCCESS)
        {
            LOG_ERR("vkCreateSwapchainKHR failed: %s (%ux%u)",
                    vk_result_to_string(create_result),
                    extent.width,
                    extent.height);
            return false;
        }
        m_swapchain = new_swapchain;
        m_swapchain_extent = extent;

        uint32_t actual = 0;
        if (!vk_check(vkGetSwapchainImagesKHR(m_device.handle(), m_swapchain, &actual, nullptr),
                      "vkGetSwapchainImagesKHR"))
        {
            destroy_swapchain();
            return false;
        }
        m_swapchain_images.resize(actual);
        if (!vk_check(vkGetSwapchainImagesKHR(m_device.handle(), m_swapchain, &actual, m_swapchain_images.data()),
                      "vkGetSwapchainImagesKHR"))
        {
            destroy_swapchain();
            return false;
        }

        // Every failure below releases the partially built swapchain
        // through destroy_swapchain, which skips null handles, so the
        // vectors are sized with null placeholders up front.
        m_swapchain_image_views.assign(actual, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < actual; ++i)
        {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = m_swapchain_images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = m_surface_format.format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            const VkResult view_result =
                vkCreateImageView(m_device.handle(), &vi, nullptr, &m_swapchain_image_views[i]);
            if (view_result != VK_SUCCESS)
            {
                LOG_ERR("vkCreateImageView (swapchain image %u) failed: %s", i, vk_result_to_string(view_result));
                destroy_swapchain();
                return false;
            }
        }

        // The depth format resolved against this device at init (see
        // resolve_depth_formats), not the nominal translation. One
        // depth image per frame slot (see m_swapchain_depth_images).
        const VkFormat depth_fmt = m_physical_device.vk_format_for(m_swapchain_depth_format);
        for (uint32_t slot = 0; slot < m_frame.frames_in_flight(); ++slot)
        {
            VkImageCreateInfo di{};
            di.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            di.imageType = VK_IMAGE_TYPE_2D;
            di.format = depth_fmt;
            di.extent = {extent.width, extent.height, 1};
            di.mipLevels = 1;
            di.arrayLayers = 1;
            di.samples = VK_SAMPLE_COUNT_1_BIT;
            di.tiling = VK_IMAGE_TILING_OPTIMAL;
            di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            di.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            const VmaAllocationCreateInfo depth_alloc = device_local_allocation();
            const VkResult depth_result = vmaCreateImage(m_device.allocator(),
                                                         &di,
                                                         &depth_alloc,
                                                         &m_swapchain_depth_images[slot],
                                                         &m_swapchain_depth_allocations[slot],
                                                         nullptr);
            if (depth_result != VK_SUCCESS)
            {
                LOG_ERR("vmaCreateImage (swapchain depth %u) failed: %s", slot, vk_result_to_string(depth_result));
                m_swapchain_depth_images[slot] = VK_NULL_HANDLE;
                m_swapchain_depth_allocations[slot] = VK_NULL_HANDLE;
                destroy_swapchain();
                return false;
            }
            VkImageViewCreateInfo dvi{};
            dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            dvi.image = m_swapchain_depth_images[slot];
            dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dvi.format = depth_fmt;
            // The swapchain depth is never sampled, so the view carries
            // every aspect the resolved format has.
            dvi.subresourceRange.aspectMask = aspect_for_vk_format(depth_fmt);
            dvi.subresourceRange.levelCount = 1;
            dvi.subresourceRange.layerCount = 1;
            const VkResult depth_view_result =
                vkCreateImageView(m_device.handle(), &dvi, nullptr, &m_swapchain_depth_views[slot]);
            if (depth_view_result != VK_SUCCESS)
            {
                LOG_ERR(
                    "vkCreateImageView (swapchain depth %u) failed: %s", slot, vk_result_to_string(depth_view_result));
                m_swapchain_depth_views[slot] = VK_NULL_HANDLE;
                destroy_swapchain();
                return false;
            }
        }

        // One render-finished semaphore per swapchain image (see the field
        // declaration). Sized to the actual image count so it tracks resize.
        VkSemaphoreCreateInfo rfi{};
        rfi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        m_render_finished.assign(actual, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < actual; ++i)
        {
            const VkResult semaphore_result =
                vkCreateSemaphore(m_device.handle(), &rfi, nullptr, &m_render_finished[i]);
            if (semaphore_result != VK_SUCCESS)
            {
                LOG_ERR("vkCreateSemaphore (render-finished %u) failed: %s", i, vk_result_to_string(semaphore_result));
                destroy_swapchain();
                return false;
            }
        }
        // Fresh images: no frame has drawn into any of them yet.
        m_image_last_slot.assign(actual, k_no_slot);

        ++m_swapchain_generation;
        LOG_INF("Vulkan swapchain: %ux%u images=%u generation=%llu",
                extent.width,
                extent.height,
                actual,
                static_cast<unsigned long long>(m_swapchain_generation));
        return true;
    }

    void vk_swapchain::destroy_swapchain()
    {
        if (m_device.handle() == VK_NULL_HANDLE)
        {
            return;
        }
        for (uint32_t slot = 0; slot < k_max_frames_in_flight; ++slot)
        {
            if (m_swapchain_depth_views[slot] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_device.handle(), m_swapchain_depth_views[slot], nullptr);
                m_swapchain_depth_views[slot] = VK_NULL_HANDLE;
            }
            if (m_swapchain_depth_images[slot] != VK_NULL_HANDLE)
            {
                vmaDestroyImage(
                    m_device.allocator(), m_swapchain_depth_images[slot], m_swapchain_depth_allocations[slot]);
                m_swapchain_depth_images[slot] = VK_NULL_HANDLE;
                m_swapchain_depth_allocations[slot] = VK_NULL_HANDLE;
            }
        }
        m_image_last_slot.clear();
        for (auto v : m_swapchain_image_views)
        {
            if (v != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_device.handle(), v, nullptr);
            }
        }
        m_swapchain_image_views.clear();
        m_swapchain_images.clear();
        for (auto s : m_render_finished)
        {
            if (s != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_device.handle(), s, nullptr);
            }
        }
        m_render_finished.clear();
        if (m_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_device.handle(), m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    void vk_swapchain::suspend_swapchain(const char* reason, bool is_error)
    {
        if (m_swapchain_suspended)
        {
            return;
        }
        m_swapchain_suspended = true;
        if (is_error)
        {
            LOG_ERR("Vulkan swapchain suspended: %s; presentation resumes once a rebuild succeeds", reason);
        }
        else
        {
            LOG_INF("Vulkan swapchain suspended: %s; presentation resumes once the surface has an extent", reason);
        }
    }

    void vk_swapchain::resume()
    {
        if (m_swapchain_suspended)
        {
            m_swapchain_suspended = false;
            LOG_INF("Vulkan swapchain resumed at %ux%u", m_swapchain_extent.width, m_swapchain_extent.height);
        }
    }

    void vk_swapchain::reset()
    {
        m_have_current_image = false;
        m_acquire_attempted = false;
        m_present_pending = false;
        m_swapchain_suspended = false;
    }

    void vk_swapchain::acquire_swapchain_image()
    {
        if (m_have_current_image || m_acquire_attempted)
        {
            return;
        }
        m_acquire_attempted = true;
        if (m_swapchain_suspended || m_swapchain == VK_NULL_HANDLE || m_device.device_lost())
        {
            return;
        }
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const VkResult r = vkAcquireNextImageKHR(m_device.handle(),
                                                     m_swapchain,
                                                     UINT64_MAX,
                                                     m_frame.image_available(),
                                                     VK_NULL_HANDLE,
                                                     &m_current_image_index);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            {
                // A suboptimal acquire still hands out an image and
                // signals the semaphore, so the frame has to use it;
                // the present's own result triggers the rebuild.
                m_have_current_image = true;
                // The images-in-flight guard: the frame that last drew
                // this image may still be executing on another slot,
                // and its submission signals the image's render-finished
                // semaphore, which this frame's submission would signal
                // again. Wait for that frame first; the presentation
                // engine normally hands an image back only after its
                // present consumed the semaphore, so this rarely blocks.
                if (m_current_image_index < m_image_last_slot.size())
                {
                    const uint32_t last_slot = m_image_last_slot[m_current_image_index];
                    if (last_slot != k_no_slot && last_slot != m_frame.frame_slot())
                    {
                        m_frame.wait_slot_fence(last_slot);
                    }
                }
                return;
            }
            if (r == VK_ERROR_OUT_OF_DATE_KHR)
            {
                // No image was acquired and the semaphore stays
                // unsignaled, so it can be reused. Rebuild against the
                // surface's current extent and retry once so the frame
                // lands in the new swapchain instead of being dropped;
                // if the rebuild suspended (minimised) or the retry is
                // out of date again, the frame skips the swapchain.
                if (attempt == 0 && m_recreate())
                {
                    continue;
                }
                return;
            }
            m_device.check_queue_result(r, "vkAcquireNextImageKHR");
            return;
        }
    }

    void vk_swapchain::note_submitted(uint32_t slot)
    {
        // This frame now owns the image until its fence retires; the
        // acquire that hands the image back checks (see
        // acquire_swapchain_image).
        if (m_current_image_index < m_image_last_slot.size())
        {
            m_image_last_slot[m_current_image_index] = slot;
        }
        // The present itself belongs to the frame boundary; end_frame
        // issues it once the renderer has closed the frame.
        m_present_pending = true;
    }

    void vk_swapchain::present()
    {
        if (m_have_current_image && m_present_pending && !m_device.device_lost())
        {
            VkPresentInfoKHR pi{};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &m_render_finished[m_current_image_index];
            pi.swapchainCount = 1;
            pi.pSwapchains = &m_swapchain;
            pi.pImageIndices = &m_current_image_index;
            const VkResult r = vkQueuePresentKHR(m_device.present_queue(), &pi);
            if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            {
                // The semaphore wait was consumed either way (a rejected
                // present still executes it), so rebuild now against the
                // surface's current extent and the next frame acquires
                // from a swapchain that matches it. The rebuild waits
                // the device idle first, which covers the command
                // buffer submitted a moment ago.
                m_recreate();
            }
            else
            {
                m_device.check_queue_result(r, "vkQueuePresentKHR");
            }
        }
        // An image acquired without a submission (the encoder failed to
        // record) is dropped rather than presented: its render-finished
        // semaphore was never signaled, so a present would wait forever.
        m_have_current_image = false;
        m_acquire_attempted = false;
        m_present_pending = false;
    }

    VkSwapchainKHR vk_swapchain::handle() const noexcept
    {
        return m_swapchain;
    }
    bool vk_swapchain::suspended() const noexcept
    {
        return m_swapchain_suspended;
    }
    VkExtent2D vk_swapchain::extent() const noexcept
    {
        return m_swapchain_extent;
    }
    VkFormat vk_swapchain::surface_format() const noexcept
    {
        return m_surface_format.format;
    }
    const std::vector<VkImageView>& vk_swapchain::image_views() const noexcept
    {
        return m_swapchain_image_views;
    }
    VkImageView vk_swapchain::depth_view(uint32_t slot) const noexcept
    {
        return m_swapchain_depth_views[slot];
    }
    texture_format vk_swapchain::depth_format() const noexcept
    {
        return m_swapchain_depth_format;
    }
    uint32_t vk_swapchain::image_count() const noexcept
    {
        return static_cast<uint32_t>(m_swapchain_images.size());
    }
    uint64_t vk_swapchain::generation() const noexcept
    {
        return m_swapchain_generation;
    }
    uint32_t vk_swapchain::current_image_index() const noexcept
    {
        return m_current_image_index;
    }
    bool vk_swapchain::have_current_image() const noexcept
    {
        return m_have_current_image;
    }
    VkSemaphore vk_swapchain::render_finished() const noexcept
    {
        return m_render_finished[m_current_image_index];
    }
} // namespace rendering_engine::gpu::backend::vulkan
