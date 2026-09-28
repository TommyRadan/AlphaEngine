// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_logical_device.cpp
 * @brief @c vk_logical_device: the VkDevice and its queues, the
 *        feature negotiation, the memory allocator and the device-lost
 *        state.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_instance.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_physical_device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    vk_logical_device::vk_logical_device(const vk_instance& instance, const vk_physical_device& physical_device)
        : m_instance{instance}, m_physical_device{physical_device}
    {
    }

    void vk_logical_device::create_logical_device(device_features& grants)
    {
        // A candidate until the feature query below confirms it.
        m_extended_dynamic_state_enabled = m_physical_device.has_extended_dynamic_state();

        const float queue_priority = 1.0f;
        std::set<uint32_t> unique_families{m_physical_device.graphics_queue_family(),
                                           m_physical_device.present_queue_family()};
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        for (uint32_t family : unique_families)
        {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = family;
            qi.queueCount = 1;
            qi.pQueuePriorities = &queue_priority;
            queue_infos.push_back(qi);
        }

        // Query the chained feature of the optional extension —
        // extended_dynamic_state exposes its feature bit through
        // @c VkPhysicalDeviceFeatures2.
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds_query{};
        eds_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 features2_query{};
        features2_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        void** features2_query_pnext = &features2_query.pNext;
        if (m_extended_dynamic_state_enabled)
        {
            *features2_query_pnext = &eds_query;
            features2_query_pnext = &eds_query.pNext;
        }
        vkGetPhysicalDeviceFeatures2(m_physical_device.handle(), &features2_query);
        if (m_extended_dynamic_state_enabled && eds_query.extendedDynamicState != VK_TRUE)
        {
            m_extended_dynamic_state_enabled = false;
            LOG_WRN("VK_EXT_extended_dynamic_state extension exposed but feature unavailable; "
                    "vertex_layout.stride==0 will collapse meshes to a point");
        }

        // Core features are requested only when the device reports
        // them: vkCreateDevice fails with VK_ERROR_FEATURE_NOT_PRESENT
        // for any unsupported feature that is asked for, and none of
        // these is universal (MoltenVK has no geometry shaders or
        // fillModeNonSolid, much mobile hardware no tessellation). What
        // was granted is recorded in grants for the consumers to
        // gate on, and every gap is logged once here.
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(m_physical_device.handle(), &supported);
        VkPhysicalDeviceFeatures features{};
        const auto request = [](VkBool32 available, VkBool32& requested, bool& granted, const char* consequence)
        {
            granted = available == VK_TRUE;
            requested = granted ? VK_TRUE : VK_FALSE;
            if (!granted)
            {
                LOG_WRN("Vulkan feature unavailable on this device: %s", consequence);
            }
        };
        request(supported.fillModeNonSolid,
                features.fillModeNonSolid,
                grants.fill_mode_non_solid,
                "fillModeNonSolid (wireframe / point materials rasterise filled)");
        request(supported.geometryShader,
                features.geometryShader,
                grants.geometry_shader,
                "geometryShader (pipelines with a geometry stage are refused)");
        request(supported.tessellationShader,
                features.tessellationShader,
                grants.tessellation_shader,
                "tessellationShader (pipelines with tessellation stages are refused)");
        request(supported.multiDrawIndirect,
                features.multiDrawIndirect,
                grants.multi_draw_indirect,
                "multiDrawIndirect (multi-draw indirect is issued as one draw per record)");
        request(supported.samplerAnisotropy,
                features.samplerAnisotropy,
                grants.sampler_anisotropy,
                "samplerAnisotropy (anisotropic filtering is unavailable)");
        request(supported.depthBiasClamp,
                features.depthBiasClamp,
                grants.depth_bias_clamp,
                "depthBiasClamp (depth bias is applied unclamped)");
        request(supported.independentBlend,
                features.independentBlend,
                grants.independent_blend,
                "independentBlend (every colour attachment of a pipeline blends alike)");
        // The block-compressed families are enabled wherever the device
        // has them, without a warning when it does not: a desktop GPU
        // lacks ASTC and a mobile one BCn as a matter of course, and
        // format_support steers the texture loaders to what is there.
        features.textureCompressionBC = supported.textureCompressionBC;
        features.textureCompressionASTC_LDR = supported.textureCompressionASTC_LDR;

        std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (m_extended_dynamic_state_enabled)
        {
            device_extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
        }
        if (m_physical_device.has_portability_subset())
        {
            // A device that lists the portability subset is a layered
            // implementation, and the spec requires the extension to
            // be enabled on it.
            device_extensions.push_back(k_portability_subset_extension);
            grants.portability_subset = true;
        }

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds_feature{};
        eds_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        eds_feature.extendedDynamicState = VK_TRUE;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.features = features;
        void** features2_pnext = &features2.pNext;
        if (m_extended_dynamic_state_enabled)
        {
            *features2_pnext = &eds_feature;
            features2_pnext = &eds_feature.pNext;
        }

        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        info.pQueueCreateInfos = queue_infos.data();
        info.pNext = &features2;
        info.pEnabledFeatures = nullptr;
        info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
        info.ppEnabledExtensionNames = device_extensions.data();
        if (!vk_check(vkCreateDevice(m_physical_device.handle(), &info, nullptr, &m_device), "vkCreateDevice"))
        {
            m_device = VK_NULL_HANDLE;
            throw std::runtime_error{"vkCreateDevice failed"};
        }
        vkGetDeviceQueue(m_device, m_physical_device.graphics_queue_family(), 0, &m_graphics_queue);
        vkGetDeviceQueue(m_device, m_physical_device.present_queue_family(), 0, &m_present_queue);

        if (m_extended_dynamic_state_enabled)
        {
            m_cmd_bind_vertex_buffers2 = reinterpret_cast<PFN_vkCmdBindVertexBuffers2EXT>(
                vkGetDeviceProcAddr(m_device, "vkCmdBindVertexBuffers2EXT"));
            if (m_cmd_bind_vertex_buffers2 == nullptr)
            {
                m_extended_dynamic_state_enabled = false;
                LOG_WRN("vkGetDeviceProcAddr returned null for vkCmdBindVertexBuffers2EXT; "
                        "falling back to non-dynamic stride");
            }
        }

        LOG_INF("Vulkan logical device created (extended_dynamic_state: %s, "
                "portability_subset: %s; features: fillModeNonSolid %s, geometryShader %s, tessellationShader %s, "
                "multiDrawIndirect %s, samplerAnisotropy %s, depthBiasClamp %s, independentBlend %s)",
                m_extended_dynamic_state_enabled ? "on" : "off",
                grants.portability_subset ? "on" : "off",
                grants.fill_mode_non_solid ? "on" : "off",
                grants.geometry_shader ? "on" : "off",
                grants.tessellation_shader ? "on" : "off",
                grants.multi_draw_indirect ? "on" : "off",
                grants.sampler_anisotropy ? "on" : "off",
                grants.depth_bias_clamp ? "on" : "off",
                grants.independent_blend ? "on" : "off");
    }

    void vk_logical_device::create_allocator()
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device.handle(), &props);
        // VMA imports the entry points of the version it is told, which
        // may be neither higher than the instance asked for nor higher
        // than the physical device implements; only major.minor count.
        const uint32_t api = std::min(m_instance.api_version(), props.apiVersion);
        const uint32_t api_major_minor = VK_MAKE_VERSION(VK_VERSION_MAJOR(api), VK_VERSION_MINOR(api), 0);

        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo info{};
        info.vulkanApiVersion = api_major_minor;
        info.instance = m_instance.handle();
        info.physicalDevice = m_physical_device.handle();
        info.device = m_device;
        info.pVulkanFunctions = &functions;
        if (!vk_check(vmaCreateAllocator(&info, &m_allocator), "vmaCreateAllocator"))
        {
            m_allocator = VK_NULL_HANDLE;
            throw std::runtime_error{"vmaCreateAllocator failed"};
        }

        const VkPhysicalDeviceMemoryProperties* memory = nullptr;
        vmaGetMemoryProperties(m_allocator, &memory);
        LOG_INF("Vulkan memory allocator: VMA %u.%u.%u against api %u.%u, %u memory heaps, %u memory types",
                VK_VERSION_MAJOR(VMA_VERSION),
                VK_VERSION_MINOR(VMA_VERSION),
                VK_VERSION_PATCH(VMA_VERSION),
                VK_VERSION_MAJOR(api_major_minor),
                VK_VERSION_MINOR(api_major_minor),
                memory != nullptr ? memory->memoryHeapCount : 0u,
                memory != nullptr ? memory->memoryTypeCount : 0u);
    }

    void vk_logical_device::destroy_allocator()
    {
        if (m_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }
    }

    void vk_logical_device::destroy()
    {
        if (m_device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
    }

    void vk_logical_device::mark_device_lost(const char* what)
    {
        if (m_device_lost)
        {
            return;
        }
        m_device_lost = true;
        LOG_FTL("%s reported VK_ERROR_DEVICE_LOST: the Vulkan device is gone. No further work is submitted or "
                "presented; the engine shuts down at the end of this frame",
                what);
    }

    bool vk_logical_device::check_queue_result(VkResult result, const char* what)
    {
        if (result == VK_SUCCESS)
        {
            return true;
        }
        if (result == VK_ERROR_DEVICE_LOST)
        {
            mark_device_lost(what);
            return false;
        }
        return vk_check(result, what);
    }

    void vk_logical_device::reset()
    {
        m_device_lost = false;
    }

    VkDevice vk_logical_device::handle() const noexcept
    {
        return m_device;
    }
    VkQueue vk_logical_device::graphics_queue() const noexcept
    {
        return m_graphics_queue;
    }
    VkQueue vk_logical_device::present_queue() const noexcept
    {
        return m_present_queue;
    }
    VmaAllocator vk_logical_device::allocator() const noexcept
    {
        return m_allocator;
    }
    bool vk_logical_device::extended_dynamic_state_enabled() const noexcept
    {
        return m_extended_dynamic_state_enabled;
    }
    PFN_vkCmdBindVertexBuffers2EXT vk_logical_device::cmd_bind_vertex_buffers2() const noexcept
    {
        return m_cmd_bind_vertex_buffers2;
    }
    bool vk_logical_device::device_lost() const noexcept
    {
        return m_device_lost;
    }
} // namespace rendering_engine::gpu::backend::vulkan
