// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file surface.hpp
 * @brief What a GPU device needs from the window it presents to, handed to
 *        @ref rendering_engine::gpu::device::init by the caller.
 *
 * The window system's objects cross as opaque pointers and its surface
 * factory as plain function pointers, so this header names neither the
 * window system nor a graphics API: the caller fills the description in
 * from its window, and the backend never looks the window up itself.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace rendering_engine::gpu
{
    /**
     * @brief Creates the presentation surface for a window on a Vulkan instance.
     * @param native_window The window, as @ref surface_desc::native_window.
     * @param instance      The @c VkInstance.
     * @param surface       Points at the @c VkSurfaceKHR that receives the surface.
     * @return false when the window system cannot create one; it has logged why.
     */
    using create_vulkan_surface_fn = bool (*)(void* native_window, void* instance, void* surface);

    /**
     * @brief Destroys a surface a @ref create_vulkan_surface_fn created.
     * @param instance The @c VkInstance it was created on.
     * @param surface  Points at the @c VkSurfaceKHR to destroy.
     */
    using destroy_vulkan_surface_fn = void (*)(void* instance, void* surface);

    /** @brief The window a device presents to, as the window system describes it. */
    struct surface_desc
    {
        /** @brief The window-system handle, passed back to the surface callbacks. */
        void* native_window{nullptr};

        /** @brief Creates the Vulkan surface for @ref native_window. */
        create_vulkan_surface_fn create_vulkan_surface{nullptr};

        /** @brief Destroys the surface @ref create_vulkan_surface made. */
        destroy_vulkan_surface_fn destroy_vulkan_surface{nullptr};

        /**
         * @brief The instance extensions the window system needs for the
         *        surface and presentation; valid while the window is alive.
         */
        std::vector<const char*> vulkan_instance_extensions;

        /**
         * @brief The drawable size in pixels at device init. Later sizes
         *        arrive through @ref device::resize_swapchain.
         */
        uint32_t width{0};
        uint32_t height{0};

        /** @brief Whether presentation waits for vertical sync. */
        bool vsync{false};
    };
} // namespace rendering_engine::gpu
