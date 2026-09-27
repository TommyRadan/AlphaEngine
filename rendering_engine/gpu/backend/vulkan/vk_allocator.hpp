// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_allocator.hpp
 * @brief The Vulkan Memory Allocator (VMA), configured for this
 *        backend, plus the allocation requests the resource TUs share.
 *
 * Every translation unit that names a VMA type or function includes
 * VMA through this header so the configuration macros are identical
 * everywhere; vk_allocator.cpp is the one TU that also defines
 * @c VMA_IMPLEMENTATION. The engine links the Vulkan loader statically
 * and calls the core entry points directly, but VMA is told to fetch
 * its own through @c vkGetInstanceProcAddr / @c vkGetDeviceProcAddr:
 * it then imports exactly the functions the instance and device
 * versions provide, and the binary carries no link-time reference to
 * a 1.3-only symbol an older loader would lack.
 */

#pragma once

#include <vulkan/vulkan.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace rendering_engine::gpu::backend::vulkan
{
    // A resource only the GPU reads and writes (vertex / index /
    // static uniform buffers, images): device-local memory, filled
    // through the staging ring.
    inline VmaAllocationCreateInfo device_local_allocation()
    {
        VmaAllocationCreateInfo info{};
        info.usage = VMA_MEMORY_USAGE_AUTO;
        return info;
    }

    // A buffer the host writes with plain memcpy and keeps mapped for
    // its whole lifetime (the per-frame and per-draw UBOs, the staging
    // ring, a dedicated staging buffer). Host-visible and coherent is
    // required, not preferred, so a write needs no flush; with
    // @p prefer_host the allocation goes to system memory (the staging
    // ring, which the GPU reads once), otherwise VMA may place it in
    // host-visible device memory when the device has some.
    inline VmaAllocationCreateInfo host_mapped_allocation(bool prefer_host)
    {
        VmaAllocationCreateInfo info{};
        info.usage = prefer_host ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        return info;
    }
} // namespace rendering_engine::gpu::backend::vulkan
