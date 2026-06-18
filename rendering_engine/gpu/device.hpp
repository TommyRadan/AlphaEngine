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
 * Owned by @ref runtime::engine through @c eng.gpu and constructed via
 * @ref create_device. Every renderable, renderer and frame driver in
 * the engine talks to this interface and never to backend-native types.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

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
    // Backends supported by @ref create_device. Both backends are
    // always compiled into the binary; the runtime choice is driven
    // by @c settings::get_graphics_backend() at engine construction.
    enum class backend_type
    {
        opengl,
        vulkan,
    };

    // Top-level GPU device interface. All resource creation,
    // destruction and command recording flows through this struct.
    // Methods are main-thread-only — there is no internal locking.
    struct device
    {
        // Out-of-line virtual destructor: pins this class's vtable
        // and typeinfo to @c device.cpp instead of emitting them in
        // every translation unit that includes this header.
        virtual ~device();

        // Bring the backend up. Must be called once after the
        // window/GL context is alive. Throws on failure.
        virtual void init() = 0;

        // Tear the backend down. Resources still alive at this
        // point are released by the backend.
        virtual void quit() = 0;

        // -- Capabilities -------------------------------------------------

        // Whether the backend can convolve image-based-lighting tables on
        // the GPU: compute pipelines writing storage images into specific
        // cube-map mip levels, plus a real mip chain to sample. Backends
        // that lack these (the Vulkan backend's mip generation and storage
        // images are still stubs) return false and the IBL builder falls
        // back to the CPU convolution. Defaults to false; the OpenGL
        // backend overrides it.
        virtual bool supports_compute_prefilter() const
        {
            return false;
        }

        // -- Resource creation --------------------------------------------

        virtual buffer create_buffer(const buffer_descriptor& descriptor) = 0;
        virtual texture create_texture(const texture_descriptor& descriptor) = 0;
        virtual sampler create_sampler(const sampler_descriptor& descriptor) = 0;
        virtual shader_module create_shader_module(const shader_module_descriptor& descriptor) = 0;
        virtual bind_group_layout create_bind_group_layout(const bind_group_layout_descriptor& descriptor) = 0;
        virtual pipeline create_pipeline(const pipeline_descriptor& descriptor) = 0;
        virtual pipeline create_compute_pipeline(const compute_pipeline_descriptor& descriptor) = 0;
        virtual bind_group create_bind_group(const bind_group_descriptor& descriptor) = 0;

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

        // Upload pixel data for a 3D texture. @p data lays out the
        // full volume in slice-major order: each z slice is a 2D
        // image of @c width * @c height texels.
        virtual void write_texture_3d(texture texture_handle, const void* data, size_t size) = 0;

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

        // Allocate an off-screen render target. The device owns the
        // colour texture (and the optional depth texture) so the
        // attachments are released together with the target. The
        // colour texture is exposed via @ref render_target_color_texture
        // so the next pass in the chain can sample it; the depth
        // attachment is internal.
        virtual render_target create_render_target(const render_target_descriptor& descriptor) = 0;

        // Release a previously created off-screen render target and
        // its owned attachments. No-op for the swapchain handle.
        virtual void destroy(render_target handle) = 0;

        // Texture handle wrapping the colour attachment of @p handle,
        // or an invalid handle for the swapchain. Used by post-process
        // passes to bind the previous pass's output as a sampled input.
        virtual texture render_target_color_texture(render_target handle) = 0;

        // Texture handle wrapping the depth attachment of @p handle, or
        // an invalid handle for the swapchain or a target created
        // without depth. The depth texture is allocated as a sampled
        // texture so a later pass can read it — the shadow pass renders
        // scene depth into a @c depth32_float target here and the lit
        // materials sample it.
        virtual texture render_target_depth_texture(render_target handle) = 0;

        // -- Command recording --------------------------------------------

        // Acquire the resources a frame needs before any pass records (the
        // swapchain image on Vulkan). Idempotent within a frame. The engine
        // calls this up front on the parallel path so the swapchain image is
        // ready before passes record on worker threads; the serial path lets
        // the backend acquire lazily, so the default is a no-op and OpenGL
        // (which has no explicit acquire) never overrides it.
        virtual void begin_frame() {}

        // Allocate a new command encoder bound to @p recording_context. A
        // backend that records passes in parallel (Vulkan) gives each context
        // its own command pool so encoders on distinct contexts record on
        // different threads without contention; single-context backends
        // (OpenGL records immediately as it is encoded) ignore the argument.
        virtual std::unique_ptr<command_encoder> create_command_encoder(uint32_t recording_context = 0) = 0;

        // Submit the encoder's recorded work for execution. After
        // this call the encoder is consumed.
        virtual void submit(std::unique_ptr<command_encoder> encoder) = 0;

        // True when the engine may record several encoders (each on its own
        // recording context) on different threads and present them together
        // via @ref submit_frame. OpenGL's immediate-mode recording is
        // single-threaded, so it returns false and the engine records serially.
        virtual bool supports_parallel_recording() const
        {
            return false;
        }

        // Upper bound on encoders that may record concurrently — the number of
        // recording contexts the backend provides. 1 when parallel recording
        // is unsupported.
        virtual uint32_t recording_context_count() const
        {
            return 1;
        }

        // Submit several encoders, each recorded into a contiguous slice of the
        // frame, as one ordered unit: @c encoders.front() executes first. The
        // default fallback submits them in order; the Vulkan backend batches
        // them into a single queue submit carrying the frame's acquire/present
        // synchronisation. Consumes the encoders.
        virtual void submit_frame(std::vector<std::unique_ptr<command_encoder>> encoders)
        {
            for (auto& encoder : encoders)
            {
                submit(std::move(encoder));
            }
        }
    };

    // Construct a concrete device for the requested backend. The
    // returned device is in a not-yet-initialised state — the caller
    // must invoke @c init() once the window/GL context is live.
    std::unique_ptr<device> create_device(backend_type type);
} // namespace rendering_engine::gpu
