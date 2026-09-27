// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gl_check.cpp
 * @brief @c gl_check_error, the body behind @c GL_CHECK.
 */

#include <rendering_engine/gpu/backend/opengl/gl_check.hpp>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    void gl_check_error(const char* expression, const char* file, int line)
    {
        // Bounded: glGetError reports one flag per call and a context
        // that has gone away could keep answering, so never spin.
        for (int drained = 0; drained < 16; ++drained)
        {
            const GLenum error = glGetError();
            if (error == GL_NO_ERROR)
            {
                return;
            }
            LOG_ERR("GL error %s (0x%x) after %s (%s:%d)", gl_error_name(error), error, expression, file, line);
        }
    }
} // namespace rendering_engine::gpu::backend::opengl
