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
