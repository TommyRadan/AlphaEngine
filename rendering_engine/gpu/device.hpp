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
 * @file device.hpp
 * @brief @c gpu::device — the backend-agnostic resource and command
 *        factory.
 *
 * Owned by @ref control::engine through @c eng.gpu and constructed via
 * @ref create_device. Every renderable, renderer and frame driver in
 * the engine talks to this interface and never to backend-native types.
 */

#pragma once

#include <cstdint>
#include <memory>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    // Backends supported by @ref create_device. Only the OpenGL
    // backend is implemented today; others compile out at the
    // factory until added.
    enum class backend_type
    {
        opengl,
    };

    // Top-level GPU device interface. All resource creation,
    // destruction and command recording flows through this struct.
    // Methods are main-thread-only — there is no internal locking.
    struct device
    {
        virtual ~device() = default;

        // Bring the backend up. Must be called once after the
        // window/GL context is alive. Throws on failure.
        virtual void init() = 0;

        // Tear the backend down. Resources still alive at this
        // point are released by the backend.
        virtual void quit() = 0;

        // -- Resource creation --------------------------------------------

        virtual buffer create_buffer(const buffer_descriptor& descriptor) = 0;
        virtual texture create_texture(const texture_descriptor& descriptor) = 0;
        virtual sampler create_sampler(const sampler_descriptor& descriptor) = 0;
        virtual shader_module create_shader_module(const shader_module_descriptor& descriptor) = 0;
        virtual bind_group_layout create_bind_group_layout(const bind_group_layout_descriptor& descriptor) = 0;
        virtual pipeline create_pipeline(const pipeline_descriptor& descriptor) = 0;
        virtual bind_group create_bind_group(const bind_group_descriptor& descriptor) = 0;

        // Replace the entries of a previously created bind group.
        // The layout is not changed; only the values bound to each
        // slot. Lets renderables hold one bind group across frames
        // and re-fill it cheaply each draw without churning the
        // resource pool.
        virtual void update_bind_group(bind_group bind_group_handle, const std::vector<binding_value>& entries) = 0;

        // -- Resource destruction -----------------------------------------

        virtual void destroy(buffer handle) = 0;
        virtual void destroy(texture handle) = 0;
        virtual void destroy(sampler handle) = 0;
        virtual void destroy(shader_module handle) = 0;
        virtual void destroy(bind_group_layout handle) = 0;
        virtual void destroy(pipeline handle) = 0;
        virtual void destroy(bind_group handle) = 0;

        // -- Resource updates ---------------------------------------------

        // Overwrite a region of @p buffer with @p size bytes from @p
        // data, starting at @p offset.
        virtual void write_buffer(buffer buffer_handle, const void* data, size_t size, size_t offset = 0) = 0;

        // Upload pixel data for a 2D texture. @p data is expected to
        // match the @c format the texture was created with. Required
        // before the texture is sampled.
        virtual void write_texture(texture texture_handle, const void* data, size_t size) = 0;

        // Upload pixel data for one face of a cube-map texture.
        virtual void write_cube_face(texture texture_handle, cube_face face, const void* data, size_t size) = 0;

        // Generate the full mip chain for a previously uploaded
        // texture. The texture must have been created with
        // @c texture_descriptor::mipmaps == @c true.
        virtual void generate_mipmaps(texture texture_handle) = 0;

        // -- Render targets -----------------------------------------------

        // Handle for the backbuffer / default framebuffer that the
        // window presents.
        virtual render_target swapchain_target() = 0;

        // Notify the device that the window backbuffer was resized.
        // The swapchain target's recorded extent updates so the next
        // pass viewport defaults match the window.
        virtual void resize_swapchain(uint32_t width, uint32_t height) = 0;

        // -- Command recording --------------------------------------------

        // Allocate a new command encoder. Each encoder records one
        // or more render passes; submission is implicit on the
        // OpenGL backend (drawing happens immediately as it's
        // recorded), but a future Vulkan backend would defer
        // execution until @ref submit.
        virtual std::unique_ptr<command_encoder> create_command_encoder() = 0;

        // Submit the encoder's recorded work for execution. After
        // this call the encoder is consumed.
        virtual void submit(std::unique_ptr<command_encoder> encoder) = 0;
    };

    // Construct a concrete device for the requested backend. The
    // returned device is in a not-yet-initialised state — the caller
    // must invoke @c init() once the window/GL context is live.
    std::unique_ptr<device> create_device(backend_type type);
} // namespace rendering_engine::gpu
