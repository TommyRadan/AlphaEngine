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
 * @file gl_device_buffer.cpp
 * @brief @c gl_device member functions that manage GL buffer objects:
 *        @c create_buffer, @c destroy(buffer), @c write_buffer.
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <glad/gl.h>

#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    buffer gl_device::create_buffer(const buffer_descriptor& descriptor)
    {
        gl_buffer record{};
        record.size = descriptor.size;
        record.usage = descriptor.usage;
        // Picking a default bind target is purely about which
        // target @c glBufferData / @c glBufferSubData target during
        // create / write — buffers are bound by name through
        // @c glBindBufferBase at draw time, so any target works
        // for the underlying object. The order below prefers
        // targets that don't share state with vertex / index
        // submission to keep the create / write path tidy.
        if ((descriptor.usage & buffer_usage_index) != 0u)
        {
            record.default_target = GL_ELEMENT_ARRAY_BUFFER;
        }
        else if ((descriptor.usage & buffer_usage_uniform) != 0u)
        {
            record.default_target = GL_UNIFORM_BUFFER;
        }
        else if ((descriptor.usage & buffer_usage_storage) != 0u)
        {
            record.default_target = GL_SHADER_STORAGE_BUFFER;
        }
        else if ((descriptor.usage & buffer_usage_indirect) != 0u)
        {
            record.default_target = GL_DRAW_INDIRECT_BUFFER;
        }
        else
        {
            record.default_target = GL_ARRAY_BUFFER;
        }

        glGenBuffers(1, &record.object_id);
        glBindBuffer(record.default_target, record.object_id);
        glBufferData(record.default_target,
                     static_cast<GLsizeiptr>(descriptor.size),
                     descriptor.initial_data,
                     to_gl_buffer_usage_hint(descriptor.hint));
        glBindBuffer(record.default_target, 0);

        buffer h{};
        h.id = m_buffers.insert(record);
        return h;
    }

    void gl_device::destroy(buffer handle)
    {
        if (auto* record = m_buffers.lookup(handle.id))
        {
            if (record->object_id != 0)
            {
                glDeleteBuffers(1, &record->object_id);
            }
            m_buffers.remove(handle.id);
        }
    }

    void gl_device::write_buffer(buffer handle, const void* data, size_t size, size_t offset)
    {
        auto* record = m_buffers.lookup(handle.id);
        if (record == nullptr || record->object_id == 0)
        {
            LOG_WRN("write_buffer: invalid buffer handle");
            return;
        }
        glBindBuffer(record->default_target, record->object_id);
        glBufferSubData(record->default_target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
        glBindBuffer(record->default_target, 0);
    }
} // namespace rendering_engine::gpu::backend::opengl
