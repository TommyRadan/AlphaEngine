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
 * @file gl_command_encoder.hpp
 * @brief OpenGL implementation of @ref gpu::command_encoder and
 *        @ref gpu::render_pass_encoder.
 *
 * GL has no real command buffer — the encoder issues GL calls directly
 * as the caller records them. The interface still mirrors WebGPU /
 * Vulkan-style scoped passes so a future explicit-API backend can plug
 * in without touching call sites.
 */

#pragma once

#include <glad/gl.h>

#include <rendering_engine/gpu/command_encoder.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        namespace backend
        {
            namespace opengl
            {
                struct gl_device;

                struct gl_render_pass_encoder : public render_pass_encoder
                {
                    gl_render_pass_encoder(gl_device& device, const render_pass_descriptor& descriptor);
                    ~gl_render_pass_encoder() override;

                    void set_pipeline(pipeline pipeline_handle) override;
                    void set_vertex_buffer(uint32_t slot,
                                           buffer buffer_handle,
                                           size_t offset,
                                           uint32_t stride_override) override;
                    void set_index_buffer(buffer buffer_handle, index_format format) override;
                    void set_bind_group(uint32_t group, bind_group bind_group_handle) override;
                    void set_viewport(int x, int y, int width, int height) override;
                    void draw(uint32_t vertex_count, uint32_t first_vertex) override;
                    void draw_indexed(uint32_t index_count, uint32_t first_index) override;
                    void end() override;

                private:
                    gl_device& m_device;

                    pipeline m_pipeline_handle{};
                    GLuint m_program_id{0};
                    GLuint m_vao_id{0};
                    GLenum m_topology{GL_TRIANGLES};

                    GLenum m_index_type{GL_UNSIGNED_INT};
                    GLsizei m_index_size{4};
                    bool m_index_buffer_bound{false};

                    bool m_active{false};
                };

                struct gl_command_encoder : public command_encoder
                {
                    explicit gl_command_encoder(gl_device& device);
                    ~gl_command_encoder() override = default;

                    std::unique_ptr<render_pass_encoder>
                    begin_render_pass(const render_pass_descriptor& descriptor) override;

                private:
                    gl_device& m_device;
                };
            } // namespace opengl
        } // namespace backend
    } // namespace gpu
} // namespace rendering_engine
