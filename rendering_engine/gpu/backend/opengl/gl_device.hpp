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
 * @file gl_device.hpp
 * @brief OpenGL 3.3 implementation of @ref gpu::device.
 *
 * The @c gl_device class is declared here in full; its member function
 * definitions are split across translation units by resource family.
 * Each split file is named @c gl_device_<resource>.cpp so the parent
 * class is explicit:
 *
 *   - gl_device.cpp           ctor/dtor, init/quit, swapchain, encoders, lookup_*
 *   - gl_device_buffer.cpp    buffer create/destroy/write
 *   - gl_device_texture.cpp   texture/sampler create/destroy/write/mipmaps
 *   - gl_device_shader.cpp    shader_module create/destroy
 *   - gl_device_pipeline.cpp  pipeline + bind_group_layout + bind_group
 *
 * Each split file can reach the private @c m_buffers, @c m_textures,
 * ... pools because they are defining members of the same class.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <rendering_engine/gpu/backend/opengl/gl_pool.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_resources.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    struct gl_device : public device
    {
        gl_device();
        ~gl_device() override;

        void init() override;
        void quit() override;

        buffer create_buffer(const buffer_descriptor& descriptor) override;
        texture create_texture(const texture_descriptor& descriptor) override;
        sampler create_sampler(const sampler_descriptor& descriptor) override;
        shader_module create_shader_module(const shader_module_descriptor& descriptor) override;
        bind_group_layout create_bind_group_layout(const bind_group_layout_descriptor& descriptor) override;
        pipeline create_pipeline(const pipeline_descriptor& descriptor) override;
        bind_group create_bind_group(const bind_group_descriptor& descriptor) override;
        void update_bind_group(bind_group bind_group_handle, const std::vector<binding_value>& entries) override;

        void destroy(buffer handle) override;
        void destroy(texture handle) override;
        void destroy(sampler handle) override;
        void destroy(shader_module handle) override;
        void destroy(bind_group_layout handle) override;
        void destroy(pipeline handle) override;
        void destroy(bind_group handle) override;

        void write_buffer(buffer buffer_handle, const void* data, size_t size, size_t offset) override;
        void write_texture(texture texture_handle, const void* data, size_t size) override;
        void write_cube_face(texture texture_handle, cube_face face, const void* data, size_t size) override;
        void generate_mipmaps(texture texture_handle) override;

        render_target swapchain_target() override;
        void resize_swapchain(uint32_t width, uint32_t height) override;
        render_target create_render_target(const render_target_descriptor& descriptor) override;
        void destroy(render_target handle) override;
        texture render_target_color_texture(render_target handle) override;

        std::unique_ptr<command_encoder> create_command_encoder() override;
        void submit(std::unique_ptr<command_encoder> encoder) override;

        // Internal accessors used by the encoder to map a
        // public handle back to its GL-side record.
        // Definitions are in gl_device.cpp.
        gl_buffer* lookup_buffer(buffer h);
        gl_texture* lookup_texture(texture h);
        gl_sampler* lookup_sampler(sampler h);
        gl_pipeline* lookup_pipeline(pipeline h);
        gl_bind_group* lookup_bind_group(bind_group h);
        gl_render_target* lookup_render_target(render_target h);
        gl_bind_group_layout* lookup_bind_group_layout(bind_group_layout h);

    private:
        gl_pool<gl_buffer> m_buffers;
        gl_pool<gl_texture> m_textures;
        gl_pool<gl_sampler> m_samplers;
        gl_pool<gl_shader_module> m_shader_modules;
        gl_pool<gl_bind_group_layout> m_bind_group_layouts;
        gl_pool<gl_pipeline> m_pipelines;
        gl_pool<gl_bind_group> m_bind_groups;
        gl_pool<gl_render_target> m_render_targets;

        render_target m_swapchain{};

        bool m_initialised{false};
    };
} // namespace rendering_engine::gpu::backend::opengl
