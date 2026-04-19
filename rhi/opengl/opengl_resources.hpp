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
 * @file opengl_resources.hpp
 * @brief Concrete GPU resource types used by the OpenGL RHI backend.
 *
 * This header and its translation unit are the only place in the codebase
 * that talks to @c glad/gl.h directly. Callers above the backend boundary
 * see these objects only as @c rhi::buffer*, @c rhi::texture*, … pointers.
 */

#pragma once

#include <map>
#include <string>

#include <glad/gl.h>

#include <rhi/rhi.hpp>

namespace rhi
{
    namespace opengl
    {
        /** @brief RAII wrapper around a GL buffer object (GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER). */
        struct gl_buffer : public rhi::buffer
        {
            GLuint id = 0;
            bool is_index = false;

            gl_buffer();
            ~gl_buffer() override;

            gl_buffer(const gl_buffer&) = delete;
            gl_buffer(gl_buffer&&) = delete;
            gl_buffer& operator=(const gl_buffer&) = delete;
            gl_buffer& operator=(gl_buffer&&) = delete;
        };

        /** @brief RAII wrapper around a GL 2D texture. */
        struct gl_texture : public rhi::texture
        {
            GLuint id = 0;

            gl_texture();
            ~gl_texture() override;

            gl_texture(const gl_texture&) = delete;
            gl_texture(gl_texture&&) = delete;
            gl_texture& operator=(const gl_texture&) = delete;
            gl_texture& operator=(gl_texture&&) = delete;
        };

        /** @brief RAII wrapper around a GL shader object. */
        struct gl_shader : public rhi::shader
        {
            GLuint id = 0;
            std::string source_code;

            gl_shader();
            ~gl_shader() override;

            gl_shader(const gl_shader&) = delete;
            gl_shader(gl_shader&&) = delete;
            gl_shader& operator=(const gl_shader&) = delete;
            gl_shader& operator=(gl_shader&&) = delete;
        };

        /** @brief RAII wrapper around a GL program. */
        struct gl_program : public rhi::program
        {
            GLuint id = 0;
            std::map<std::string, GLint> uniforms;

            gl_program();
            ~gl_program() override;

            gl_program(const gl_program&) = delete;
            gl_program(gl_program&&) = delete;
            gl_program& operator=(const gl_program&) = delete;
            gl_program& operator=(gl_program&&) = delete;
        };

        /** @brief RAII wrapper around a GL vertex array object. */
        struct gl_vertex_array : public rhi::vertex_array
        {
            GLuint id = 0;

            gl_vertex_array();
            ~gl_vertex_array() override;

            gl_vertex_array(const gl_vertex_array&) = delete;
            gl_vertex_array(gl_vertex_array&&) = delete;
            gl_vertex_array& operator=(const gl_vertex_array&) = delete;
            gl_vertex_array& operator=(gl_vertex_array&&) = delete;
        };

        /** @brief RAII wrapper around a GL draw framebuffer plus its color/depth textures. */
        struct gl_framebuffer : public rhi::framebuffer
        {
            GLuint id = 0;
            gl_texture color_tex;
            gl_texture depth_tex;

            gl_framebuffer();
            ~gl_framebuffer() override;

            gl_framebuffer(const gl_framebuffer&) = delete;
            gl_framebuffer(gl_framebuffer&&) = delete;
            gl_framebuffer& operator=(const gl_framebuffer&) = delete;
            gl_framebuffer& operator=(gl_framebuffer&&) = delete;
        };
    } // namespace opengl
} // namespace rhi
