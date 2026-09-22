/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
