// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_instance.cpp
 * @brief @c vk_instance: the Vulkan instance, the validation layer's
 *        debug messenger, the VK_EXT_debug_utils entry points and the
 *        window surface.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_instance.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        constexpr const char* k_validation_layer = "VK_LAYER_KHRONOS_validation";

        // Spelled out rather than taken from the header so the build
        // does not depend on a Vulkan SDK new enough to define them:
        // VK_KHR_portability_enumeration arrived in header 1.3.216.
        // The flag value is VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR.
        constexpr const char* k_portability_enumeration_extension = "VK_KHR_portability_enumeration";
        constexpr VkInstanceCreateFlags k_enumerate_portability_flag = 0x00000001;

        bool layer_available(const char* name)
        {
            uint32_t count = 0;
            if (!vk_check(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties"))
            {
                return false;
            }
            std::vector<VkLayerProperties> layers(count);
            const VkResult r = vkEnumerateInstanceLayerProperties(&count, layers.data());
            if (r != VK_SUCCESS && r != VK_INCOMPLETE)
            {
                vk_check(r, "vkEnumerateInstanceLayerProperties");
                return false;
            }
            for (const auto& layer : layers)
            {
                if (std::strcmp(layer.layerName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // Whether the instance (or, with @p layer, that layer) exposes
        // the extension @p name. Every instance extension the backend
        // asks for beyond the window system's mandatory set goes through
        // here first, so vkCreateInstance never fails on an optional one.
        bool instance_extension_available(const char* name, const char* layer = nullptr)
        {
            uint32_t count = 0;
            if (!vk_check(vkEnumerateInstanceExtensionProperties(layer, &count, nullptr),
                          "vkEnumerateInstanceExtensionProperties"))
            {
                return false;
            }
            std::vector<VkExtensionProperties> extensions(count);
            const VkResult r = vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data());
            if (r != VK_SUCCESS && r != VK_INCOMPLETE)
            {
                vk_check(r, "vkEnumerateInstanceExtensionProperties");
                return false;
            }
            for (const auto& extension : extensions)
            {
                if (std::strcmp(extension.extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // The highest instance-level API version the loader supports.
        // vkEnumerateInstanceVersion is a 1.1 entry point: a 1.0 loader
        // has no symbol for it, so it is resolved through
        // vkGetInstanceProcAddr and its absence means 1.0.
        uint32_t probe_instance_version()
        {
            auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
                vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
            if (enumerate == nullptr)
            {
                return VK_API_VERSION_1_0;
            }
            uint32_t version = VK_API_VERSION_1_0;
            if (enumerate(&version) != VK_SUCCESS)
            {
                return VK_API_VERSION_1_0;
            }
            return version;
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                      void* /*user_data*/)
        {
            if (data == nullptr || data->pMessage == nullptr)
            {
                return VK_FALSE;
            }
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                LOG_ERR("Vulkan: %s", data->pMessage);
            }
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                LOG_WRN("Vulkan: %s", data->pMessage);
            }
            else
            {
                LOG_INF("Vulkan: %s", data->pMessage);
            }
            return VK_FALSE;
        }
    } // namespace

    void vk_instance::create_instance(const std::vector<const char*>& window_extensions)
    {
        // The backend needs the 1.1 core feature queries
        // (vkGetPhysicalDeviceFeatures2) and asks for up to 1.2, which
        // is what it was written against; a newer loader is asked for
        // 1.2 (it accepts any version it supports), an older one for
        // exactly what it has, and a 1.0 loader is refused outright
        // rather than failing later inside vkCreateInstance.
        const uint32_t instance_version = probe_instance_version();
        if (instance_version < VK_API_VERSION_1_1)
        {
            LOG_FTL("Vulkan loader supports API %u.%u only; the Vulkan backend needs 1.1 or newer "
                    "(update the graphics driver / Vulkan runtime)",
                    VK_VERSION_MAJOR(instance_version),
                    VK_VERSION_MINOR(instance_version));
            throw std::runtime_error{"Vulkan 1.1 or newer is required"};
        }
        const uint32_t api_version = std::min<uint32_t>(instance_version, VK_API_VERSION_1_2);

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "AlphaEngine";
        app.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
        app.pEngineName = "AlphaEngine";
        app.engineVersion = VK_MAKE_VERSION(0, 0, 1);
        app.apiVersion = api_version;

        std::vector<const char*> extensions = window_extensions;
        VkInstanceCreateFlags flags = 0;
        // A layered implementation (MoltenVK on macOS / iOS) only
        // enumerates its non-conformant physical devices when the
        // application opts in with the portability extension + flag;
        // without them vkEnumeratePhysicalDevices finds nothing.
        const bool portability_enumeration = instance_extension_available(k_portability_enumeration_extension);
        if (portability_enumeration)
        {
            extensions.push_back(k_portability_enumeration_extension);
            flags |= k_enumerate_portability_flag;
        }
        std::vector<const char*> layers;
        // VK_EXT_debug_utils carries the validation layer's messages
        // and, independently of validation, the command labels and
        // object names a graphics debugger shows. It is requested
        // whenever the instance exposes it, confirmed first so a
        // missing extension never fails vkCreateInstance.
        m_debug_utils_enabled = instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#ifdef _DEBUG
        if (layer_available(k_validation_layer))
        {
            // The layer itself may be what provides the extension.
            if (!m_debug_utils_enabled &&
                instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, k_validation_layer))
            {
                m_debug_utils_enabled = true;
            }
            if (m_debug_utils_enabled)
            {
                layers.push_back(k_validation_layer);
                m_validation_enabled = true;
            }
            else
            {
                LOG_WRN("Vulkan validation layer is available but VK_EXT_debug_utils is not; "
                        "debug-build run will not be validated");
            }
        }
        else
        {
            LOG_WRN("Vulkan validation layer not available; debug-build run will not be validated");
        }
#endif
        if (m_debug_utils_enabled)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.flags = flags;
        info.pApplicationInfo = &app;
        info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        const VkResult create_result = vkCreateInstance(&info, nullptr, &m_instance);
        if (create_result != VK_SUCCESS)
        {
            LOG_FTL("vkCreateInstance failed: %s", vk_result_to_string(create_result));
            throw std::runtime_error{"vkCreateInstance failed"};
        }
        m_api_version = api_version;
        LOG_INF("Vulkan instance: loader %u.%u.%u, requested api %u.%u, portability enumeration %s, validation %s, "
                "debug utils %s",
                VK_VERSION_MAJOR(instance_version),
                VK_VERSION_MINOR(instance_version),
                VK_VERSION_PATCH(instance_version),
                VK_VERSION_MAJOR(api_version),
                VK_VERSION_MINOR(api_version),
                portability_enumeration ? "on" : "off",
                m_validation_enabled ? "on" : "off",
                m_debug_utils_enabled ? "on" : "off");
    }

    void vk_instance::load_debug_utils_functions()
    {
        m_cmd_begin_debug_label = nullptr;
        m_cmd_end_debug_label = nullptr;
        m_set_debug_object_name = nullptr;
        if (!m_debug_utils_enabled || m_instance == VK_NULL_HANDLE)
        {
            return;
        }
        m_cmd_begin_debug_label = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT"));
        m_cmd_end_debug_label = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT"));
        m_set_debug_object_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectNameEXT"));
        // Labels and names go together: a loader that resolves only
        // part of the extension gets neither, so the feature flag is
        // an honest answer.
        if (m_cmd_begin_debug_label == nullptr || m_cmd_end_debug_label == nullptr ||
            m_set_debug_object_name == nullptr)
        {
            LOG_WRN("VK_EXT_debug_utils is enabled but its label entry points did not resolve; "
                    "debug groups and object names are off");
            m_cmd_begin_debug_label = nullptr;
            m_cmd_end_debug_label = nullptr;
            m_set_debug_object_name = nullptr;
        }
    }

    void vk_instance::name_object(VkDevice device, VkObjectType type, uint64_t object_handle, const char* name) const
    {
        if (m_set_debug_object_name == nullptr || device == VK_NULL_HANDLE || object_handle == 0 || name == nullptr)
        {
            return;
        }
        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = object_handle;
        info.pObjectName = name;
        m_set_debug_object_name(device, &info);
    }

    void vk_instance::create_debug_messenger()
    {
        if (!m_validation_enabled)
        {
            return;
        }
        auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create_fn == nullptr)
        {
            return;
        }
        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = debug_callback;
        if (create_fn(m_instance, &info, nullptr, &m_debug_messenger) != VK_SUCCESS)
        {
            m_debug_messenger = VK_NULL_HANDLE;
        }
    }

    void vk_instance::destroy_debug_messenger()
    {
        if (m_debug_messenger == VK_NULL_HANDLE)
        {
            return;
        }
        auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_fn != nullptr)
        {
            destroy_fn(m_instance, m_debug_messenger, nullptr);
        }
        m_debug_messenger = VK_NULL_HANDLE;
    }

    void vk_instance::create_surface(const surface_desc& surface)
    {
        if (surface.native_window == nullptr || surface.create_vulkan_surface == nullptr)
        {
            LOG_FTL("vk_instance::create_surface: window subsystem missing");
            throw std::runtime_error{"vk_device: window missing"};
        }
        if (!surface.create_vulkan_surface(surface.native_window, m_instance, &m_surface))
        {
            // The window system has logged its reason.
            throw std::runtime_error{"vk_device: window surface creation failed"};
        }
        m_destroy_surface = surface.destroy_vulkan_surface;
    }

    void vk_instance::shutdown()
    {
        if (m_surface != VK_NULL_HANDLE)
        {
            if (m_destroy_surface != nullptr)
            {
                m_destroy_surface(m_instance, &m_surface);
            }
            m_surface = VK_NULL_HANDLE;
        }
        m_destroy_surface = nullptr;
        destroy_debug_messenger();
        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
        m_debug_utils_enabled = false;
        m_cmd_begin_debug_label = nullptr;
        m_cmd_end_debug_label = nullptr;
        m_set_debug_object_name = nullptr;
    }

    VkInstance vk_instance::handle() const noexcept
    {
        return m_instance;
    }
    VkSurfaceKHR vk_instance::surface() const noexcept
    {
        return m_surface;
    }
    uint32_t vk_instance::api_version() const noexcept
    {
        return m_api_version;
    }
    bool vk_instance::debug_utils_enabled() const noexcept
    {
        return m_debug_utils_enabled;
    }
    bool vk_instance::object_names_loaded() const noexcept
    {
        return m_set_debug_object_name != nullptr;
    }
    PFN_vkCmdBeginDebugUtilsLabelEXT vk_instance::cmd_begin_debug_label() const noexcept
    {
        return m_cmd_begin_debug_label;
    }
    PFN_vkCmdEndDebugUtilsLabelEXT vk_instance::cmd_end_debug_label() const noexcept
    {
        return m_cmd_end_debug_label;
    }
} // namespace rendering_engine::gpu::backend::vulkan
