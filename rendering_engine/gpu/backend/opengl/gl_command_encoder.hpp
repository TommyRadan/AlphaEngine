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
 * as the caller records them. The interface still mirrors the scoped
 * passes of the Vulkan backend, so call sites are the same for both.
 * Every piece of context state the encoders set goes through the
 * device's @c gl_state_cache, which is trusted only within a pass:
 * @c begin and @c end both invalidate it.
 */

#pragma once

#include <array>

#include <glad/gl.h>

#include <rendering_engine/gpu/command_encoder.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    struct gl_device;

    struct gl_render_pass_encoder : public render_pass_encoder
    {
        gl_render_pass_encoder(gl_device& device, const render_pass_descriptor& descriptor);
        ~gl_render_pass_encoder() override;

        void set_pipeline(pipeline pipeline_handle) override;
        void set_vertex_buffer(uint32_t slot, buffer buffer_handle, size_t offset, uint32_t stride_override) override;
        void set_index_buffer(buffer buffer_handle, index_format format) override;
        void set_bind_group(uint32_t group,
                            bind_group bind_group_handle,
                            std::span<const uint32_t> dynamic_offsets) override;
        // Records nothing: ARB_gl_spirv programs have no push constants
        // (device_features::push_constants is false here). Reported once
        // per pipeline.
        void push_constants(shader_stages stages, uint32_t offset, uint32_t size, const void* data) override;
        void set_viewport(int x, int y, int width, int height) override;
        void set_scissor(int x, int y, int width, int height) override;
        void set_stencil_reference(uint32_t reference) override;
        void
        draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) override;
        void draw_indexed(uint32_t index_count,
                          uint32_t instance_count,
                          uint32_t first_index,
                          int32_t base_vertex,
                          uint32_t first_instance) override;
        void draw_indexed_indirect(buffer indirect_buffer, size_t offset) override;
        void multi_draw_indexed_indirect(buffer indirect_buffer,
                                         size_t offset,
                                         uint32_t draw_count,
                                         uint32_t stride) override;
        void end() override;

    private:
        gl_device& m_device;

        // The target and the parts of the pass descriptor @ref end
        // acts on: which attachments may be discarded, and whether the
        // pass drives depth state at all. @c m_color_count is how many
        // colour attachments the target has (one for the swapchain).
        render_target m_target{};
        std::array<store_op, max_color_attachments> m_color_store{};
        uint32_t m_color_count{0};
        store_op m_depth_store{store_op::store};
        bool m_use_depth{true};

        pipeline m_pipeline_handle{};
        GLuint m_program_id{0};
        GLuint m_vao_id{0};
        GLenum m_topology{GL_TRIANGLES};

        // The dynamic stencil reference the bound pipeline's stencil
        // test compares against; re-applied on every set_pipeline.
        uint32_t m_stencil_reference{0};

        GLenum m_index_type{GL_UNSIGNED_INT};
        GLsizei m_index_size{4};
        bool m_index_buffer_bound{false};

        bool m_active{false};
    };

    struct gl_compute_pass_encoder : public compute_pass_encoder
    {
        explicit gl_compute_pass_encoder(gl_device& device);
        ~gl_compute_pass_encoder() override;

        void set_pipeline(pipeline pipeline_handle) override;
        void set_bind_group(uint32_t group,
                            bind_group bind_group_handle,
                            std::span<const uint32_t> dynamic_offsets) override;
        void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override;
        void end() override;

    private:
        gl_device& m_device;

        pipeline m_pipeline_handle{};
        GLuint m_program_id{0};
        bool m_active{false};
    };

    struct gl_command_encoder : public command_encoder
    {
        explicit gl_command_encoder(gl_device& device);
        ~gl_command_encoder() override = default;

        std::unique_ptr<render_pass_encoder> begin_render_pass(const render_pass_descriptor& descriptor) override;
        std::unique_ptr<compute_pass_encoder> begin_compute_pass() override;

        void copy_buffer_to_buffer(buffer src, size_t src_offset, buffer dst, size_t dst_offset, size_t size) override;
        void clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value) override;
        void barrier(pipeline_stage src_stage,
                     pipeline_stage dst_stage,
                     access_flag src_access,
                     access_flag dst_access) override;
        void
        copy_buffer_to_texture(buffer src, size_t src_offset, texture dst, const texture_copy_region& region) override;
        void
        copy_texture_to_buffer(texture src, const texture_copy_region& region, buffer dst, size_t dst_offset) override;
        void push_debug_group(const char* name) override;
        void pop_debug_group() override;
        void reset_queries(query_set set, uint32_t first, uint32_t count) override;
        void write_timestamp(query_set set, uint32_t index) override;

    private:
        gl_device& m_device;
        // Open debug groups, so a pop never underflows the driver's
        // stack when a caller pops more than it pushed.
        uint32_t m_debug_group_depth{0};
    };
} // namespace rendering_engine::gpu::backend::opengl
