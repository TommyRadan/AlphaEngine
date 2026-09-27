// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file factory.cpp
 * @brief Backend dispatch for @ref rendering_engine::gpu::create_device.
 *
 * Each concrete backend lives behind a small @c make_*_device free
 * function in its own translation unit (@c gl_factory.cpp,
 * @c vk_factory.cpp). Both backends are always linked into the
 * binary; the runtime choice is driven by
 * @c core::settings::graphics.backend, which the engine consults at
 * construction time.
 */

#include <stdexcept>

#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    std::unique_ptr<device> make_gl_device();
} // namespace rendering_engine::gpu::backend::opengl

namespace rendering_engine::gpu::backend::vulkan
{
    std::unique_ptr<device> make_vk_device();
} // namespace rendering_engine::gpu::backend::vulkan

namespace rendering_engine::gpu
{
    std::unique_ptr<device> create_device(backend_type type)
    {
        switch (type)
        {
        case backend_type::opengl:
            return backend::opengl::make_gl_device();
        case backend_type::vulkan:
            return backend::vulkan::make_vk_device();
        }
        LOG_FTL("create_device: unknown backend_type");
        throw std::runtime_error{"create_device: unknown backend_type"};
    }
} // namespace rendering_engine::gpu
