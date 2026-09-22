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
 * @file gl_state_cache.hpp
 * @brief Shadow of the GL context state the encoders set, so a pass
 *        that binds the same program / VAO / texture / blend state for
 *        consecutive draws issues each GL call once.
 *
 * The cache is only trusted while the backend alone talks to the
 * context. @c gl_device::invalidate_state_cache forgets everything at
 * every pass boundary (the debug overlay records ImGui draws into the
 * open debug pass) and whenever a GL object is deleted (GL recycles
 * object names, so a shadow holding a deleted name would wrongly skip
 * the rebind of its successor). Within a pass every setter compares
 * against the shadow and skips the call on a match.
 */

#pragma once

#include <array>
#include <cstdint>

#include <glad/gl.h>

namespace rendering_engine::gpu::backend::opengl
{
    // One piece of shadowed state: the last value this backend set, or
    // unknown after an invalidate.
    template<typename T>
    struct gl_cached
    {
        T value{};
        bool known{false};

        // True when the GL call must go out (the shadow is unknown or
        // disagrees); records @p next as the new shadow either way.
        bool update(const T& next)
        {
            if (known && value == next)
            {
                return false;
            }
            value = next;
            known = true;
            return true;
        }

        void reset()
        {
            known = false;
        }
    };

    struct gl_state_cache
    {
        // Texture units / sampler slots and indexed buffer binding
        // points with a shadow. Higher binding numbers are bound
        // unconditionally; the engine's bindings stay far below this.
        static constexpr uint32_t cached_units = 32;
        static constexpr uint32_t cached_buffer_bindings = 32;

        void invalidate();

        void use_program(GLuint program);
        void bind_vertex_array(GLuint vertex_array);
        void bind_framebuffer(GLuint framebuffer);
        void bind_draw_indirect_buffer(GLuint buffer);
        void set_blend(bool enabled, GLenum src, GLenum dst, GLenum equation);
        void set_depth_test(bool enabled);
        void set_depth_write(bool enabled);
        void set_depth_func(GLenum func);
        void set_cull(bool enabled, GLenum face);
        void set_front_face(GLenum mode);
        void set_polygon_mode(GLenum mode);
        void set_scissor_test(bool enabled);
        void set_viewport(GLint x, GLint y, GLsizei width, GLsizei height);
        void bind_texture_unit(GLuint unit, GLuint texture);
        void bind_sampler(GLuint unit, GLuint sampler);
        void bind_uniform_buffer(GLuint index, GLuint buffer);
        void bind_storage_buffer(GLuint index, GLuint buffer);

    private:
        struct blend_func
        {
            GLenum src{GL_ONE};
            GLenum dst{GL_ZERO};
            GLenum equation{GL_FUNC_ADD};
            bool operator==(const blend_func&) const = default;
        };

        struct viewport_rect
        {
            GLint x{0};
            GLint y{0};
            GLsizei width{0};
            GLsizei height{0};
            bool operator==(const viewport_rect&) const = default;
        };

        gl_cached<GLuint> m_program;
        gl_cached<GLuint> m_vertex_array;
        gl_cached<GLuint> m_framebuffer;
        gl_cached<GLuint> m_draw_indirect_buffer;
        gl_cached<bool> m_blend_enabled;
        gl_cached<blend_func> m_blend_func;
        gl_cached<bool> m_depth_test;
        gl_cached<bool> m_depth_write;
        gl_cached<GLenum> m_depth_func;
        gl_cached<bool> m_cull_enabled;
        gl_cached<GLenum> m_cull_face;
        gl_cached<GLenum> m_front_face;
        gl_cached<GLenum> m_polygon_mode;
        gl_cached<bool> m_scissor_test;
        gl_cached<viewport_rect> m_viewport;
        std::array<gl_cached<GLuint>, cached_units> m_textures;
        std::array<gl_cached<GLuint>, cached_units> m_samplers;
        std::array<gl_cached<GLuint>, cached_buffer_bindings> m_uniform_buffers;
        std::array<gl_cached<GLuint>, cached_buffer_bindings> m_storage_buffers;
    };
} // namespace rendering_engine::gpu::backend::opengl
