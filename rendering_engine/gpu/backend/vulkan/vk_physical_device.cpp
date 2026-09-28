// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_physical_device.cpp
 * @brief @c vk_physical_device: GPU selection and the depth formats
 *        negotiated against the chosen one.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_physical_device.hpp>

#include <array>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_instance.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        const char* depth_format_name(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_D16_UNORM:
                return "D16_UNORM";
            case VK_FORMAT_X8_D24_UNORM_PACK32:
                return "X8_D24_UNORM_PACK32";
            case VK_FORMAT_D32_SFLOAT:
                return "D32_SFLOAT";
            case VK_FORMAT_D16_UNORM_S8_UINT:
                return "D16_UNORM_S8_UINT";
            case VK_FORMAT_D24_UNORM_S8_UINT:
                return "D24_UNORM_S8_UINT";
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return "D32_SFLOAT_S8_UINT";
            default:
                return "<not a depth format>";
            }
        }
    } // namespace

    void vk_physical_device::pick_physical_device(const vk_instance& instance)
    {
        uint32_t count = 0;
        if (!vk_check(vkEnumeratePhysicalDevices(instance.handle(), &count, nullptr), "vkEnumeratePhysicalDevices") ||
            count == 0)
        {
            throw std::runtime_error{"no Vulkan-capable GPUs"};
        }
        std::vector<VkPhysicalDevice> gpus(count);
        const VkResult enumerate_result = vkEnumeratePhysicalDevices(instance.handle(), &count, gpus.data());
        if (enumerate_result != VK_SUCCESS && enumerate_result != VK_INCOMPLETE)
        {
            vk_check(enumerate_result, "vkEnumeratePhysicalDevices");
            throw std::runtime_error{"vkEnumeratePhysicalDevices failed"};
        }

        VkPhysicalDevice best = VK_NULL_HANDLE;
        bool best_discrete = false;
        uint32_t best_graphics = 0;
        uint32_t best_present = 0;
        uint32_t best_timestamp_bits = 0;

        for (auto gpu : gpus)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(gpu, &props);
            // The device-level 1.1 functionality the backend relies on
            // (vkGetPhysicalDeviceFeatures2 against this device) is
            // gated by the physical device's own version, not the
            // instance's.
            if (props.apiVersion < VK_API_VERSION_1_1)
            {
                LOG_WRN("Vulkan GPU %s skipped: api %u.%u, the backend needs 1.1",
                        props.deviceName,
                        VK_VERSION_MAJOR(props.apiVersion),
                        VK_VERSION_MINOR(props.apiVersion));
                continue;
            }

            uint32_t qf_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qf_count, nullptr);
            std::vector<VkQueueFamilyProperties> qfs(qf_count);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qf_count, qfs.data());
            std::optional<uint32_t> graphics_family;
            std::optional<uint32_t> present_family;
            for (uint32_t i = 0; i < qf_count; ++i)
            {
                if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && !graphics_family.has_value())
                {
                    graphics_family = i;
                }
                VkBool32 present_supported = VK_FALSE;
                if (!vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, instance.surface(), &present_supported),
                              "vkGetPhysicalDeviceSurfaceSupportKHR"))
                {
                    present_supported = VK_FALSE;
                }
                if (present_supported == VK_TRUE && !present_family.has_value())
                {
                    present_family = i;
                }
            }
            if (!graphics_family.has_value() || !present_family.has_value())
            {
                continue;
            }

            uint32_t ext_count = 0;
            if (!vk_check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr),
                          "vkEnumerateDeviceExtensionProperties"))
            {
                continue;
            }
            std::vector<VkExtensionProperties> exts(ext_count);
            const VkResult ext_result = vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, exts.data());
            if (ext_result != VK_SUCCESS && ext_result != VK_INCOMPLETE)
            {
                vk_check(ext_result, "vkEnumerateDeviceExtensionProperties");
                continue;
            }
            bool has_swap = false;
            bool has_extended_dynamic_state = false;
            bool has_portability_subset = false;
            for (const auto& e : exts)
            {
                if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                {
                    has_swap = true;
                }
                else if (std::strcmp(e.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0)
                {
                    has_extended_dynamic_state = true;
                }
                else if (std::strcmp(e.extensionName, k_portability_subset_extension) == 0)
                {
                    has_portability_subset = true;
                }
            }
            if (!has_swap)
            {
                continue;
            }

            const bool discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (best == VK_NULL_HANDLE || (discrete && !best_discrete))
            {
                best = gpu;
                best_discrete = discrete;
                best_graphics = *graphics_family;
                best_present = *present_family;
                best_timestamp_bits = qfs[*graphics_family].timestampValidBits;
                m_has_extended_dynamic_state = has_extended_dynamic_state;
                m_has_portability_subset = has_portability_subset;
            }
        }
        if (best == VK_NULL_HANDLE)
        {
            LOG_FTL("No suitable Vulkan GPU: needs api 1.1, a graphics queue that can present to the window, "
                    "and VK_KHR_swapchain");
            throw std::runtime_error{"no suitable Vulkan GPU"};
        }

        m_physical_device = best;
        m_graphics_queue_family = best_graphics;
        m_present_queue_family = best_present;
        m_timestamp_valid_bits = best_timestamp_bits;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &props);
        LOG_INF("Vulkan GPU: %s (api %u.%u.%u)",
                props.deviceName,
                VK_VERSION_MAJOR(props.apiVersion),
                VK_VERSION_MINOR(props.apiVersion),
                VK_VERSION_PATCH(props.apiVersion));

        // Every staging-ring reservation starts at a multiple of the
        // device's preferred copy alignment (a power of two on every
        // known driver; rounded up in case), never below 16 so any
        // texel block the engine's formats have divides it, and capped
        // so a driver that prefers page alignment does not waste a page
        // per small upload.
        VkDeviceSize alignment = 16;
        while (alignment < props.limits.optimalBufferCopyOffsetAlignment && alignment < 4096)
        {
            alignment *= 2;
        }
        m_copy_offset_alignment = alignment;
    }

    void vk_physical_device::resolve_depth_formats()
    {
        const auto supports_depth_attachment = [this](VkFormat format)
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
        };
        struct slot
        {
            texture_format format;
            const char* name;
            size_t index;
        };
        const std::array<slot, 3> slots{{{texture_format::depth24, "depth24", 0},
                                         {texture_format::depth32_float, "depth32_float", 1},
                                         {texture_format::depth24_stencil8, "depth24_stencil8", 2}}};
        for (const slot& s : slots)
        {
            const VkFormat preferred = to_vk_format(s.format);
            const VkFormat chosen = select_depth_format(s.format, supports_depth_attachment);
            if (chosen == VK_FORMAT_UNDEFINED)
            {
                LOG_FTL("Vulkan: no depth attachment format available for %s (the spec mandates D32_SFLOAT)", s.name);
                throw std::runtime_error{"no Vulkan depth attachment format"};
            }
            m_depth_formats[s.index] = chosen;
            if (chosen == preferred)
            {
                LOG_INF("Vulkan depth format %s: %s", s.name, depth_format_name(chosen));
            }
            else
            {
                LOG_INF("Vulkan depth format %s: %s (%s is not a depth attachment format on this device)",
                        s.name,
                        depth_format_name(chosen),
                        depth_format_name(preferred));
            }
        }
    }

    void vk_physical_device::reset()
    {
        m_has_portability_subset = false;
        m_timestamp_valid_bits = 0;
        m_depth_formats.fill(VK_FORMAT_UNDEFINED);
    }

    VkPhysicalDevice vk_physical_device::handle() const noexcept
    {
        return m_physical_device;
    }
    uint32_t vk_physical_device::graphics_queue_family() const noexcept
    {
        return m_graphics_queue_family;
    }
    uint32_t vk_physical_device::present_queue_family() const noexcept
    {
        return m_present_queue_family;
    }
    uint32_t vk_physical_device::timestamp_valid_bits() const noexcept
    {
        return m_timestamp_valid_bits;
    }
    bool vk_physical_device::has_extended_dynamic_state() const noexcept
    {
        return m_has_extended_dynamic_state;
    }
    bool vk_physical_device::has_portability_subset() const noexcept
    {
        return m_has_portability_subset;
    }
    VkDeviceSize vk_physical_device::copy_offset_alignment() const noexcept
    {
        return m_copy_offset_alignment;
    }
    VkFormat vk_physical_device::vk_format_for(texture_format format) const noexcept
    {
        VkFormat resolved = VK_FORMAT_UNDEFINED;
        switch (format)
        {
        case texture_format::depth24:
            resolved = m_depth_formats[0];
            break;
        case texture_format::depth32_float:
            resolved = m_depth_formats[1];
            break;
        case texture_format::depth24_stencil8:
            resolved = m_depth_formats[2];
            break;
        default:
            break;
        }
        // Before resolve_depth_formats has run (or for a colour
        // format) the nominal translation stands.
        return resolved != VK_FORMAT_UNDEFINED ? resolved : to_vk_format(format);
    }
} // namespace rendering_engine::gpu::backend::vulkan
