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

#include <rhi/null/null_rhi.hpp>

namespace rhi
{
    namespace null
    {
        namespace
        {
            struct null_buffer : public rhi::buffer
            {
            };
            struct null_texture : public rhi::texture
            {
            };
            struct null_shader : public rhi::shader
            {
            };
            struct null_program : public rhi::program
            {
            };
            struct null_vertex_array : public rhi::vertex_array
            {
            };
            struct null_framebuffer : public rhi::framebuffer
            {
                null_texture color_tex;
                null_texture depth_tex;
            };
        } // namespace

        void null_rhi::init() {}
        void null_rhi::quit() {}

        buffer* null_rhi::create_buffer(const buffer_desc& /*desc*/)
        {
            return new null_buffer();
        }
        void null_rhi::destroy_buffer(buffer* b)
        {
            delete b;
        }
        void
        null_rhi::buffer_sub_data(buffer* /*b*/, std::size_t /*offset*/, std::size_t /*length*/, const void* /*data*/)
        {
        }

        texture* null_rhi::create_texture(const texture_desc& /*desc*/)
        {
            return new null_texture();
        }
        void null_rhi::destroy_texture(texture* t)
        {
            delete t;
        }
        void null_rhi::texture_set_wrap(texture* /*t*/, wrap_mode /*s*/, wrap_mode /*tt*/, wrap_mode /*r*/) {}
        void null_rhi::texture_set_filters(texture* /*t*/, filter_mode /*min_filter*/, filter_mode /*mag_filter*/) {}
        void null_rhi::texture_generate_mipmaps(texture* /*t*/) {}

        shader* null_rhi::create_shader(shader_stage /*stage*/, const std::string& /*source*/)
        {
            return new null_shader();
        }
        void null_rhi::destroy_shader(shader* s)
        {
            delete s;
        }

        program* null_rhi::create_program()
        {
            return new null_program();
        }
        void null_rhi::destroy_program(program* p)
        {
            delete p;
        }
        void null_rhi::program_attach(program* /*p*/, const shader* /*s*/) {}
        void null_rhi::program_link(program* /*p*/) {}
        void null_rhi::program_start(program* /*p*/) {}
        void null_rhi::program_stop(program* /*p*/) {}

        int32_t null_rhi::program_get_uniform_location(program* /*p*/, const std::string& /*name*/)
        {
            return -1;
        }
        void null_rhi::program_set_uniform(program* /*p*/, int32_t /*location*/, int32_t /*value*/) {}
        void null_rhi::program_set_uniform(program* /*p*/, int32_t /*location*/, float /*value*/) {}
        void null_rhi::program_set_uniform(program* /*p*/, int32_t /*location*/, const glm::vec2& /*value*/) {}
        void null_rhi::program_set_uniform(program* /*p*/, int32_t /*location*/, const glm::vec3& /*value*/) {}
        void null_rhi::program_set_uniform(program* /*p*/, int32_t /*location*/, const glm::vec4& /*value*/) {}
        void null_rhi::program_set_uniform(program* /*p*/, int32_t /*location*/, const glm::mat3& /*value*/) {}
        void null_rhi::program_set_uniform(program* /*p*/, int32_t /*location*/, const glm::mat4& /*value*/) {}

        vertex_array* null_rhi::create_vertex_array()
        {
            return new null_vertex_array();
        }
        void null_rhi::destroy_vertex_array(vertex_array* v)
        {
            delete v;
        }
        void null_rhi::vertex_array_bind_attribute(vertex_array* /*v*/, const vertex_attribute_desc& /*desc*/) {}
        void null_rhi::vertex_array_bind_elements(vertex_array* /*v*/, const buffer* /*elements*/) {}

        framebuffer* null_rhi::create_framebuffer(const framebuffer_desc& /*desc*/)
        {
            return new null_framebuffer();
        }
        void null_rhi::destroy_framebuffer(framebuffer* f)
        {
            delete f;
        }
        const texture* null_rhi::framebuffer_color_texture(const framebuffer* f)
        {
            return &static_cast<const null_framebuffer*>(f)->color_tex;
        }
        const texture* null_rhi::framebuffer_depth_texture(const framebuffer* f)
        {
            return &static_cast<const null_framebuffer*>(f)->depth_tex;
        }

        void null_rhi::enable(capability /*cap*/) {}
        void null_rhi::disable(capability /*cap*/) {}
        void null_rhi::set_clear_color(float /*r*/, float /*g*/, float /*b*/, float /*a*/) {}
        void null_rhi::clear(clear_buffer /*buffers*/) {}
        void null_rhi::set_depth_mask(bool /*write_enabled*/) {}
        void null_rhi::set_blend_func_alpha() {}

        void null_rhi::bind_texture(const texture* /*t*/, uint8_t /*unit*/) {}
        void null_rhi::bind_framebuffer(const framebuffer* /*f*/) {}
        void null_rhi::bind_default_framebuffer() {}

        void null_rhi::draw(const draw_call& /*call*/) {}
    } // namespace null
} // namespace rhi
