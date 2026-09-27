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
 * @file gl_state_cache.cpp
 * @brief @c gl_state_cache setters: compare against the shadow, issue
 *        the GL call only on a change.
 */

#include <rendering_engine/gpu/backend/opengl/gl_state_cache.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
        void set_capability(GLenum capability, bool enabled)
        {
            if (enabled)
            {
                glEnable(capability);
            }
            else
            {
                glDisable(capability);
            }
        }

        void set_indexed_capability(GLenum capability, GLuint index, bool enabled)
        {
            if (enabled)
            {
                glEnablei(capability, index);
            }
            else
            {
                glDisablei(capability, index);
            }
        }

        // An indexed buffer binding: the whole buffer when @p size is 0,
        // otherwise @p size bytes from @p offset.
        void bind_buffer_range(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
        {
            if (size == 0)
            {
                glBindBufferBase(target, index, buffer);
            }
            else
            {
                glBindBufferRange(target, index, buffer, offset, size);
            }
        }
    } // namespace

    void gl_state_cache::invalidate()
    {
        m_program.reset();
        m_vertex_array.reset();
        m_framebuffer.reset();
        m_draw_indirect_buffer.reset();
        for (auto& entry : m_blend_enabled)
        {
            entry.reset();
        }
        for (auto& entry : m_blend_func)
        {
            entry.reset();
        }
        for (auto& entry : m_color_mask)
        {
            entry.reset();
        }
        m_depth_test.reset();
        m_depth_write.reset();
        m_depth_func.reset();
        m_stencil_test.reset();
        for (auto& entry : m_stencil_func)
        {
            entry.reset();
        }
        for (auto& entry : m_stencil_op)
        {
            entry.reset();
        }
        for (auto& entry : m_stencil_mask)
        {
            entry.reset();
        }
        m_polygon_offset_enabled.reset();
        m_polygon_offset.reset();
        m_cull_enabled.reset();
        m_cull_face.reset();
        m_front_face.reset();
        m_polygon_mode.reset();
        m_scissor_test.reset();
        m_scissor.reset();
        m_viewport.reset();
        for (auto& unit : m_textures)
        {
            unit.reset();
        }
        for (auto& unit : m_samplers)
        {
            unit.reset();
        }
        for (auto& binding : m_uniform_buffers)
        {
            binding.reset();
        }
        for (auto& binding : m_storage_buffers)
        {
            binding.reset();
        }
    }

    void gl_state_cache::use_program(GLuint program)
    {
        if (m_program.update(program))
        {
            glUseProgram(program);
        }
    }

    void gl_state_cache::bind_vertex_array(GLuint vertex_array)
    {
        if (m_vertex_array.update(vertex_array))
        {
            glBindVertexArray(vertex_array);
        }
    }

    void gl_state_cache::bind_framebuffer(GLuint framebuffer)
    {
        if (m_framebuffer.update(framebuffer))
        {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        }
    }

    void gl_state_cache::bind_draw_indirect_buffer(GLuint buffer)
    {
        if (m_draw_indirect_buffer.update(buffer))
        {
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer);
        }
    }

    void gl_state_cache::set_blend(GLuint index, bool enabled, GLenum src, GLenum dst, GLenum equation)
    {
        if (index >= max_color_attachments)
        {
            return;
        }
        if (m_blend_enabled[index].update(enabled))
        {
            set_indexed_capability(GL_BLEND, index, enabled);
        }
        if (!enabled)
        {
            return;
        }
        if (m_blend_func[index].update(blend_func{src, dst, equation}))
        {
            glBlendFunci(index, src, dst);
            glBlendEquationi(index, equation);
        }
    }

    void gl_state_cache::set_color_mask(GLuint index, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
    {
        if (index >= max_color_attachments)
        {
            return;
        }
        if (m_color_mask[index].update(color_mask{red, green, blue, alpha}))
        {
            glColorMaski(index, red, green, blue, alpha);
        }
    }

    void gl_state_cache::set_depth_test(bool enabled)
    {
        if (m_depth_test.update(enabled))
        {
            set_capability(GL_DEPTH_TEST, enabled);
        }
    }

    void gl_state_cache::set_depth_write(bool enabled)
    {
        if (m_depth_write.update(enabled))
        {
            glDepthMask(enabled ? GL_TRUE : GL_FALSE);
        }
    }

    void gl_state_cache::set_depth_func(GLenum func)
    {
        if (m_depth_func.update(func))
        {
            glDepthFunc(func);
        }
    }

    void gl_state_cache::set_stencil_test(bool enabled)
    {
        if (m_stencil_test.update(enabled))
        {
            set_capability(GL_STENCIL_TEST, enabled);
        }
    }

    void gl_state_cache::set_stencil_func(GLenum face, GLenum func, GLint reference, GLuint mask)
    {
        if (m_stencil_func[face_index(face)].update(stencil_func{func, reference, mask}))
        {
            glStencilFuncSeparate(face, func, reference, mask);
        }
    }

    void gl_state_cache::set_stencil_op(GLenum face, GLenum fail, GLenum depth_fail, GLenum depth_pass)
    {
        if (m_stencil_op[face_index(face)].update(stencil_ops{fail, depth_fail, depth_pass}))
        {
            glStencilOpSeparate(face, fail, depth_fail, depth_pass);
        }
    }

    void gl_state_cache::set_stencil_mask(GLenum face, GLuint mask)
    {
        if (m_stencil_mask[face_index(face)].update(mask))
        {
            glStencilMaskSeparate(face, mask);
        }
    }

    void gl_state_cache::set_polygon_offset(bool enabled, float factor, float units, float clamp)
    {
        if (m_polygon_offset_enabled.update(enabled))
        {
            set_capability(GL_POLYGON_OFFSET_FILL, enabled);
            set_capability(GL_POLYGON_OFFSET_LINE, enabled);
            set_capability(GL_POLYGON_OFFSET_POINT, enabled);
        }
        if (!enabled)
        {
            return;
        }
        if (m_polygon_offset.update(polygon_offset{factor, units, clamp}))
        {
            // Core in 4.6 (ARB_polygon_offset_clamp); a clamp of 0
            // disables the clamp, matching the abstract state.
            glPolygonOffsetClamp(factor, units, clamp);
        }
    }

    void gl_state_cache::set_cull(bool enabled, GLenum face)
    {
        if (m_cull_enabled.update(enabled))
        {
            set_capability(GL_CULL_FACE, enabled);
        }
        if (enabled && m_cull_face.update(face))
        {
            glCullFace(face);
        }
    }

    void gl_state_cache::set_front_face(GLenum mode)
    {
        if (m_front_face.update(mode))
        {
            glFrontFace(mode);
        }
    }

    void gl_state_cache::set_polygon_mode(GLenum mode)
    {
        if (m_polygon_mode.update(mode))
        {
            glPolygonMode(GL_FRONT_AND_BACK, mode);
        }
    }

    void gl_state_cache::set_scissor_test(bool enabled)
    {
        if (m_scissor_test.update(enabled))
        {
            set_capability(GL_SCISSOR_TEST, enabled);
        }
    }

    void gl_state_cache::set_scissor(GLint x, GLint y, GLsizei width, GLsizei height)
    {
        if (m_scissor.update(rect{x, y, width, height}))
        {
            glScissor(x, y, width, height);
        }
    }

    void gl_state_cache::set_viewport(GLint x, GLint y, GLsizei width, GLsizei height)
    {
        if (m_viewport.update(rect{x, y, width, height}))
        {
            glViewport(x, y, width, height);
        }
    }

    void gl_state_cache::bind_texture_unit(GLuint unit, GLuint texture)
    {
        // glBindTextureUnit binds the texture to its own target on the
        // unit without touching the active-texture selector, so no
        // glActiveTexture round trip is needed (or cached).
        if (unit >= cached_units || m_textures[unit].update(texture))
        {
            glBindTextureUnit(unit, texture);
        }
    }

    void gl_state_cache::bind_sampler(GLuint unit, GLuint sampler)
    {
        if (unit >= cached_units || m_samplers[unit].update(sampler))
        {
            glBindSampler(unit, sampler);
        }
    }

    void gl_state_cache::bind_uniform_buffer(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
    {
        if (index >= cached_buffer_bindings || m_uniform_buffers[index].update(buffer_range{buffer, offset, size}))
        {
            bind_buffer_range(GL_UNIFORM_BUFFER, index, buffer, offset, size);
        }
    }

    void gl_state_cache::bind_storage_buffer(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
    {
        if (index >= cached_buffer_bindings || m_storage_buffers[index].update(buffer_range{buffer, offset, size}))
        {
            bind_buffer_range(GL_SHADER_STORAGE_BUFFER, index, buffer, offset, size);
        }
    }
} // namespace rendering_engine::gpu::backend::opengl
