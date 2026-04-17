/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#pragma once

#include <infrastructure/singleton.hpp>
#include <rendering_engine/opengl/framebuffer.hpp>
#include <rendering_engine/opengl/program.hpp>
#include <rendering_engine/opengl/shader.hpp>
#include <rendering_engine/opengl/texture.hpp>
#include <rendering_engine/opengl/typedef.hpp>
#include <rendering_engine/opengl/vertex_array.hpp>
#include <rendering_engine/opengl/vertex_buffer.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine
{
    namespace opengl
    {
        struct context : public singleton<context>
        {
            void init();
            void quit();

            opengl::framebuffer* create_framebuffer(uint32_t, uint32_t, uint8_t, uint8_t);
            opengl::program* create_program();
            opengl::shader* create_shader(shader_type);
            opengl::texture* create_texture();
            opengl::vertex_array* create_vao();
            opengl::vertex_buffer* create_vbo();

            void delete_framebuffer(const opengl::framebuffer*);
            void delete_program(const opengl::program*);
            void delete_shader(const opengl::shader*);
            void delete_texture(const opengl::texture*);
            void delete_vab(const opengl::vertex_array*);
            void delete_vbo(const opengl::vertex_buffer*);

            void enable(opengl::capability capability);
            void disable(opengl::capability capability);

            void clear_color(const rendering_engine::util::color& color);
            void clear(opengl::buffer buffers);
            void depth_mask(bool write_enabled);

            void bind_texture(const opengl::texture& texture, const unsigned char unit);
            void bind_framebuffer(const opengl::framebuffer& framebuffer);
            void bind_framebuffer();

            void
            draw_arrays(const opengl::vertex_array& vao, opengl::primitive mode, unsigned int offset, size_t vertices);

            void draw_elements(const opengl::vertex_array& vao,
                              opengl::primitive mode,
                              intptr_t offset,
                              unsigned int count,
                              rendering_engine::opengl::type type);
        };
    } // namespace opengl
} // namespace rendering_engine
