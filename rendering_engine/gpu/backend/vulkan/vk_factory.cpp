// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_factory.cpp
 * @brief Vulkan backend entry point. The shared
 *        @ref rendering_engine::gpu::create_device dispatch in
 *        @c rendering_engine/gpu/factory.cpp calls into here for
 *        @c backend_type::vulkan.
 */

#include <memory>

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    std::unique_ptr<device> make_vk_device()
    {
        return std::make_unique<vk_device>();
    }
} // namespace rendering_engine::gpu::backend::vulkan
