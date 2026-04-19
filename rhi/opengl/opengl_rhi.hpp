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
 * @file opengl_rhi.hpp
 * @brief OpenGL implementation of the @c rhi::device interface.
 *
 * Concrete backend driven by glad (OpenGL 3.3 Core). Safe to include from
 * the engine bootstrap code that owns the SDL GL context — callers above
 * the renderer layer should only use @c rhi::device / @c rhi::get_device.
 */

#pragma once

#include <rhi/rhi.hpp>

namespace rhi
{
    namespace opengl
    {
        /**
         * @brief Loads GL entry points via glad and services every RHI call
         *        against the currently-bound OpenGL context.
         *
         * Instances are owned by @c rendering_engine::context. All methods
         * must run on the main thread (the one that owns the GL context).
         */
        class opengl_rhi : public rhi::device
        {
        public:
            opengl_rhi() = default;
            ~opengl_rhi() override = default;

            opengl_rhi(const opengl_rhi&) = delete;
            opengl_rhi(opengl_rhi&&) = delete;
            opengl_rhi& operator=(const opengl_rhi&) = delete;
            opengl_rhi& operator=(opengl_rhi&&) = delete;

            void init() override;
            void quit() override;

            buffer* create_buffer(const buffer_desc& desc) override;
            void destroy_buffer(buffer* b) override;
            void buffer_sub_data(buffer* b, std::size_t offset, std::size_t length, const void* data) override;

            texture* create_texture(const texture_desc& desc) override;
            void destroy_texture(texture* t) override;
            void texture_set_wrap(texture* t, wrap_mode s, wrap_mode tt, wrap_mode r) override;
            void texture_set_filters(texture* t, filter_mode min_filter, filter_mode mag_filter) override;
            void texture_generate_mipmaps(texture* t) override;

            shader* create_shader(shader_stage stage, const std::string& source) override;
            void destroy_shader(shader* s) override;

            program* create_program() override;
            void destroy_program(program* p) override;
            void program_attach(program* p, const shader* s) override;
            void program_link(program* p) override;
            void program_start(program* p) override;
            void program_stop(program* p) override;

            int32_t program_get_uniform_location(program* p, const std::string& name) override;
            void program_set_uniform(program* p, int32_t location, int32_t value) override;
            void program_set_uniform(program* p, int32_t location, float value) override;
            void program_set_uniform(program* p, int32_t location, const glm::vec2& value) override;
            void program_set_uniform(program* p, int32_t location, const glm::vec3& value) override;
            void program_set_uniform(program* p, int32_t location, const glm::vec4& value) override;
            void program_set_uniform(program* p, int32_t location, const glm::mat3& value) override;
            void program_set_uniform(program* p, int32_t location, const glm::mat4& value) override;

            vertex_array* create_vertex_array() override;
            void destroy_vertex_array(vertex_array* v) override;
            void vertex_array_bind_attribute(vertex_array* v, const vertex_attribute_desc& desc) override;
            void vertex_array_bind_elements(vertex_array* v, const buffer* elements) override;

            framebuffer* create_framebuffer(const framebuffer_desc& desc) override;
            void destroy_framebuffer(framebuffer* f) override;
            const texture* framebuffer_color_texture(const framebuffer* f) override;
            const texture* framebuffer_depth_texture(const framebuffer* f) override;

            void enable(capability cap) override;
            void disable(capability cap) override;
            void set_clear_color(float r, float g, float b, float a) override;
            void clear(clear_buffer buffers) override;
            void set_depth_mask(bool write_enabled) override;
            void set_blend_func_alpha() override;

            void bind_texture(const texture* t, uint8_t unit) override;
            void bind_framebuffer(const framebuffer* f) override;
            void bind_default_framebuffer() override;

            void draw(const draw_call& call) override;
        };
    } // namespace opengl
} // namespace rhi
