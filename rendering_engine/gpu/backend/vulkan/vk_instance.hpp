// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_instance.hpp
 * @brief The Vulkan instance, the validation layer's debug messenger,
 *        the VK_EXT_debug_utils entry points and the window surface.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/surface.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    // The first component vk_device brings up and the last it takes
    // down. init calls create_instance, load_debug_utils_functions,
    // create_debug_messenger and create_surface in that order;
    // shutdown releases the surface, the messenger and the instance,
    // and runs once the logical device is gone.
    class vk_instance
    {
    public:
        // Create the instance at the API version the loader and the
        // backend agree on (1.1 at least, 1.2 at most), with the window
        // system's extensions, the portability enumeration and
        // VK_EXT_debug_utils when they are exposed, and in Debug builds
        // the Khronos validation layer. Throws when the loader is too
        // old or vkCreateInstance fails.
        void create_instance(const std::vector<const char*>& window_extensions);
        // Resolve the VK_EXT_debug_utils label / object-name entry
        // points once the instance exists; leaves them null (and the
        // debug_labels feature off) when the extension is not enabled.
        void load_debug_utils_functions();
        // Route the validation layer's warnings and errors into the log;
        // nothing without the layer.
        void create_debug_messenger();
        // Create the window's surface through the window system's
        // factory; throws when there is no window or it fails.
        void create_surface(const surface_desc& surface);
        // Release the surface, the debug messenger and the instance, in
        // that order, and forget the debug-utils entry points.
        void shutdown();

        VkInstance handle() const noexcept;
        VkSurfaceKHR surface() const noexcept;
        // The API version the instance was created with.
        uint32_t api_version() const noexcept;
        // VK_EXT_debug_utils is enabled on the instance.
        bool debug_utils_enabled() const noexcept;
        // The object-name entry point resolved (and with it the labels).
        bool object_names_loaded() const noexcept;
        PFN_vkCmdBeginDebugUtilsLabelEXT cmd_begin_debug_label() const noexcept;
        PFN_vkCmdEndDebugUtilsLabelEXT cmd_end_debug_label() const noexcept;

        // Name @p object_handle of @p type, an object of @p device, for
        // debuggers and validation messages through
        // vkSetDebugUtilsObjectNameEXT; no-op without the extension.
        void name_object(VkDevice device, VkObjectType type, uint64_t object_handle, const char* name) const;

    private:
        void destroy_debug_messenger();

        VkInstance m_instance{VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
        VkSurfaceKHR m_surface{VK_NULL_HANDLE};
        // Releases m_surface through the window system that created it
        // (surface_desc::destroy_vulkan_surface).
        destroy_vulkan_surface_fn m_destroy_surface{nullptr};
        // The API version the instance was created with (what VMA is
        // told, capped by the physical device's own version).
        uint32_t m_api_version{VK_API_VERSION_1_1};
        bool m_validation_enabled{false};
        // VK_EXT_debug_utils is enabled on the instance: labels and
        // object names reach validation messages and debuggers.
        bool m_debug_utils_enabled{false};
        PFN_vkCmdBeginDebugUtilsLabelEXT m_cmd_begin_debug_label{nullptr};
        PFN_vkCmdEndDebugUtilsLabelEXT m_cmd_end_debug_label{nullptr};
        PFN_vkSetDebugUtilsObjectNameEXT m_set_debug_object_name{nullptr};
    };
} // namespace rendering_engine::gpu::backend::vulkan
