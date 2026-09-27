// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gl_factory.cpp
 * @brief OpenGL backend entry point. The shared
 *        @ref rendering_engine::gpu::create_device dispatch in
 *        @c rendering_engine/gpu/factory.cpp calls into here for
 *        @c backend_type::opengl.
 */

#include <memory>

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    std::unique_ptr<device> make_gl_device()
    {
        return std::make_unique<gl_device>();
    }
} // namespace rendering_engine::gpu::backend::opengl
