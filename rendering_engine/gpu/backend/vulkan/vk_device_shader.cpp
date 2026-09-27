// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device_shader.cpp
 * @brief @c vk_device::create_shader_module wraps the SPIR-V byte
 *        blob the engine has already produced via
 *        @ref gpu::compile_glsl_to_spirv into a @c VkShaderModule.
 *        The Vulkan backend never sees GLSL source.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <core/log.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    shader_module vk_device::create_shader_module(const shader_module_descriptor& descriptor)
    {
        if (descriptor.spirv.empty())
        {
            LOG_ERR("vk_device::create_shader_module: empty SPIR-V");
            return {};
        }

        vk_shader_module record{};
        record.stage = descriptor.stage;

        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = descriptor.spirv.size() * sizeof(uint32_t);
        info.pCode = descriptor.spirv.data();
        if (!vk_check(vkCreateShaderModule(m_device, &info, nullptr, &record.object), "vkCreateShaderModule"))
        {
            return {};
        }

        shader_module h{};
        h.id = m_shader_modules.insert(record);
        return h;
    }

    void vk_device::destroy(shader_module handle)
    {
        auto* record = m_shader_modules.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        // Graphics pipelines materialise lazily, per render pass, from
        // the stored descriptor (graphics_pipeline_for), so a module a
        // caller releases right after create_pipeline may still be
        // needed by a variant built later in the same frame. Deferring
        // the destroy to the next frame-top drain, like every other
        // resource, keeps that build valid.
        const VkDevice dev = m_device;
        const VkShaderModule object = record->object;
        if (object != VK_NULL_HANDLE)
        {
            enqueue_destroy([dev, object] { vkDestroyShaderModule(dev, object, nullptr); });
        }
        record->object = VK_NULL_HANDLE;
        m_shader_modules.remove(handle.id);
    }
} // namespace rendering_engine::gpu::backend::vulkan
