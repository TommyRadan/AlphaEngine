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
 * @file gl_device_shader.cpp
 * @brief @c gl_device member functions that compile shader stages:
 *        @c create_shader_module, @c destroy(shader_module).
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <stdexcept>
#include <string>

#include <glad/gl.h>

#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    shader_module gl_device::create_shader_module(const shader_module_descriptor& descriptor)
    {
        gl_shader_module record{};
        record.stage = descriptor.stage;
        record.object_id = glCreateShader(to_gl_shader_stage(descriptor.stage));
        if (record.object_id == 0)
        {
            LOG_FTL("create_shader_module: glCreateShader returned 0");
            throw std::runtime_error{"shader creation failed"};
        }

        const char* source = descriptor.source.c_str();
        const auto length = static_cast<GLint>(descriptor.source.length());
        glShaderSource(record.object_id, 1, &source, &length);
        glCompileShader(record.object_id);

        GLint compiled = 0;
        glGetShaderiv(record.object_id, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE)
        {
            GLint log_length = 0;
            glGetShaderiv(record.object_id, GL_INFO_LOG_LENGTH, &log_length);
            std::string info_log(log_length > 0 ? static_cast<size_t>(log_length) : 1u, '\0');
            if (log_length > 0)
            {
                glGetShaderInfoLog(record.object_id, log_length, nullptr, info_log.data());
            }
            LOG_ERR("Shader compile failed: %s", info_log.c_str());
            glDeleteShader(record.object_id);
            throw std::runtime_error{info_log};
        }

        LOG_INF("Compiled shader id=%u stage=%i", record.object_id, static_cast<int>(descriptor.stage));

        shader_module h{};
        h.id = m_shader_modules.insert(record);
        return h;
    }

    void gl_device::destroy(shader_module handle)
    {
        if (auto* record = m_shader_modules.lookup(handle.id))
        {
            if (record->object_id != 0)
            {
                glDeleteShader(record->object_id);
            }
            m_shader_modules.remove(handle.id);
        }
    }
} // namespace rendering_engine::gpu::backend::opengl
