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

#include <rendering_engine/gpu/render_target.hpp>

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
        // Blend state and channel mask of draw buffer @p index: every
        // attachment is driven through the indexed entry points
        // (glEnablei / glBlendFunci / glColorMaski), so a pipeline
        // that blends attachment 1 differently from attachment 0 and
        // one that blends every attachment alike go through the same
        // per-index shadow.
        void set_blend(GLuint index, bool enabled, GLenum src, GLenum dst, GLenum equation);
        void set_color_mask(GLuint index, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
        void set_depth_test(bool enabled);
        void set_depth_write(bool enabled);
        void set_depth_func(GLenum func);
        void set_stencil_test(bool enabled);
        // @p face is GL_FRONT or GL_BACK.
        void set_stencil_func(GLenum face, GLenum func, GLint reference, GLuint mask);
        void set_stencil_op(GLenum face, GLenum fail, GLenum depth_fail, GLenum depth_pass);
        void set_stencil_mask(GLenum face, GLuint mask);
        // Polygon offset for every polygon mode at once (fill, line,
        // point), so a pipeline's depth bias follows its polygon mode.
        void set_polygon_offset(bool enabled, float factor, float units, float clamp);
        void set_cull(bool enabled, GLenum face);
        void set_front_face(GLenum mode);
        void set_polygon_mode(GLenum mode);
        void set_scissor_test(bool enabled);
        void set_scissor(GLint x, GLint y, GLsizei width, GLsizei height);
        void set_viewport(GLint x, GLint y, GLsizei width, GLsizei height);
        void bind_texture_unit(GLuint unit, GLuint texture);
        void bind_sampler(GLuint unit, GLuint sampler);
        // Indexed buffer bindings. A @p size of 0 binds the whole
        // buffer (@c glBindBufferBase, @p offset must be 0); otherwise
        // @p size bytes from @p offset (@c glBindBufferRange). The
        // shadow keeps the range, so a dynamic-offset slot rebound per
        // draw at a new offset reaches the driver each time while a
        // repeat of the same range does not.
        void bind_uniform_buffer(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
        void bind_storage_buffer(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);

    private:
        struct blend_func
        {
            GLenum src{GL_ONE};
            GLenum dst{GL_ZERO};
            GLenum equation{GL_FUNC_ADD};
            bool operator==(const blend_func&) const = default;
        };

        struct color_mask
        {
            GLboolean red{GL_TRUE};
            GLboolean green{GL_TRUE};
            GLboolean blue{GL_TRUE};
            GLboolean alpha{GL_TRUE};
            bool operator==(const color_mask&) const = default;
        };

        struct stencil_func
        {
            GLenum func{GL_ALWAYS};
            GLint reference{0};
            GLuint mask{0xFFu};
            bool operator==(const stencil_func&) const = default;
        };

        struct stencil_ops
        {
            GLenum fail{GL_KEEP};
            GLenum depth_fail{GL_KEEP};
            GLenum depth_pass{GL_KEEP};
            bool operator==(const stencil_ops&) const = default;
        };

        struct polygon_offset
        {
            float factor{0.0f};
            float units{0.0f};
            float clamp{0.0f};
            bool operator==(const polygon_offset&) const = default;
        };

        struct buffer_range
        {
            GLuint buffer{0};
            GLintptr offset{0};
            GLsizeiptr size{0};
            bool operator==(const buffer_range&) const = default;
        };

        struct rect
        {
            GLint x{0};
            GLint y{0};
            GLsizei width{0};
            GLsizei height{0};
            bool operator==(const rect&) const = default;
        };

        // Front / back stencil shadows are indexed 0 / 1.
        static constexpr size_t face_index(GLenum face)
        {
            return face == GL_BACK ? 1u : 0u;
        }

        gl_cached<GLuint> m_program;
        gl_cached<GLuint> m_vertex_array;
        gl_cached<GLuint> m_framebuffer;
        gl_cached<GLuint> m_draw_indirect_buffer;
        std::array<gl_cached<bool>, max_color_attachments> m_blend_enabled;
        std::array<gl_cached<blend_func>, max_color_attachments> m_blend_func;
        std::array<gl_cached<color_mask>, max_color_attachments> m_color_mask;
        gl_cached<bool> m_depth_test;
        gl_cached<bool> m_depth_write;
        gl_cached<GLenum> m_depth_func;
        gl_cached<bool> m_stencil_test;
        std::array<gl_cached<stencil_func>, 2> m_stencil_func;
        std::array<gl_cached<stencil_ops>, 2> m_stencil_op;
        std::array<gl_cached<GLuint>, 2> m_stencil_mask;
        gl_cached<bool> m_polygon_offset_enabled;
        gl_cached<polygon_offset> m_polygon_offset;
        gl_cached<bool> m_cull_enabled;
        gl_cached<GLenum> m_cull_face;
        gl_cached<GLenum> m_front_face;
        gl_cached<GLenum> m_polygon_mode;
        gl_cached<bool> m_scissor_test;
        gl_cached<rect> m_scissor;
        gl_cached<rect> m_viewport;
        std::array<gl_cached<GLuint>, cached_units> m_textures;
        std::array<gl_cached<GLuint>, cached_units> m_samplers;
        std::array<gl_cached<buffer_range>, cached_buffer_bindings> m_uniform_buffers;
        std::array<gl_cached<buffer_range>, cached_buffer_bindings> m_storage_buffers;
    };
} // namespace rendering_engine::gpu::backend::opengl
