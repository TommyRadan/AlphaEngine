// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gl_check.hpp
 * @brief @c GL_CHECK — a debug-build @c glGetError drain wrapped around
 *        the GL calls that can fail on bad arguments (storage allocation,
 *        uploads, vertex-format baking, framebuffer wiring, SPIR-V
 *        specialisation).
 *
 * The KHR_debug callback @c gl_device installs reports the same errors
 * synchronously when the driver granted a debug context; @c GL_CHECK
 * names the offending expression and call site, and still works on a
 * driver that ignored the debug-context request. It expands to the bare
 * expression in release builds, so it is never on a per-draw path.
 */

#pragma once

#include <glad/gl.h>

namespace rendering_engine::gpu::backend::opengl
{
    // Drain every pending GL error, logging each with its enum name,
    // the expression that produced it and where it was issued.
    void gl_check_error(const char* expression, const char* file, int line);
} // namespace rendering_engine::gpu::backend::opengl

#if _DEBUG
#define GL_CHECK(expression)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        expression;                                                                                                    \
        ::rendering_engine::gpu::backend::opengl::gl_check_error(#expression, __FILE__, __LINE__);                     \
    } while (false)
#else
#define GL_CHECK(expression)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        expression;                                                                                                    \
    } while (false)
#endif
