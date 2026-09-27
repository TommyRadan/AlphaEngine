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
    // by @c core::settings::graphics.backend at engine construction.
    enum class backend_type
    {
        opengl,
        vulkan,
    };

    // What the device can do, filled by the backend during @c init
    // from the context / physical device it brought up and read
    // through @ref device::features. Every flag is a hard gate:
    // consumers ask before they rely on the feature, and a backend
    // that lacks it either degrades (a wireframe material rasterises
    // filled, multi-draw indirect unrolls, anisotropy is ignored) or
    // refuses (a pipeline with an ungranted stage). The OpenGL 4.6
    // core profile grants almost everything; the Vulkan backend
    // requests each optional core feature only when the physical
    // device reports it and records the grant here.
    struct device_features
    {
        // Compute pipelines and storage bindings.
        bool compute{false};
        // @c draw_indexed_indirect; @c multi_draw_indirect adds a draw
        // count above one per call.
        bool indirect_draw{false};
        bool multi_draw_indirect{false};
        bool geometry_shader{false};
        bool tessellation_shader{false};
        // @c polygon_mode::line / @c point (Vulkan's fillModeNonSolid).
        bool fill_mode_non_solid{false};
        // @c sampler_descriptor::max_anisotropy above 1.
        bool sampler_anisotropy{false};
        // @c depth_bias_state::clamp (Vulkan's depthBiasClamp).
        bool depth_bias_clamp{false};
        // Differing @c pipeline_descriptor::attachment_blend entries
        // (Vulkan's independentBlend).
        bool independent_blend{false};
        // @c command_encoder::write_timestamp produces readable ticks.
        bool timestamp_queries{false};
        // Debug groups and object names reach a debugger / the driver's
        // debug output (KHR_debug; VK_EXT_debug_utils enabled).
        bool debug_labels{false};
        // The device runs on a layered implementation
        // (VK_KHR_portability_subset — MoltenVK). Informational.
        bool portability_subset{false};
        // The image-based-lighting tables can be convolved on the GPU:
        // compute pipelines writing storage images into specific
        // cube-map mip levels, plus a real mip chain to sample. Both
        // backends implement this today; the IBL builder falls back to
        // the CPU convolution when it is false.
        bool compute_prefilter{false};
        // @c render_pass_encoder::push_constants reaches the shaders
        // through the pipeline's @c push_constant_ranges, with at least
        // @c min_push_constants_size bytes (see
        // @c device_limits::max_push_constants_size). Set on Vulkan when
        // maxPushConstantsSize reaches that minimum, which every
        // conformant device does. OpenGL loads SPIR-V through
        // ARB_gl_spirv, which has no push constants, so it is false
        // there. Every library shader module of a device with the
        // feature is compiled with @c AE_PUSH_CONSTANTS defined (see
        // @c create_library_shader_module), so a shader declares a
        // push-constant block only where the device has one.
        bool push_constants{false};
    };

    // Numeric limits of the device, filled beside @ref device_features.
    struct device_limits
    {
        uint32_t max_texture_size_2d{0};
        uint32_t max_texture_size_3d{0};
        uint32_t max_texture_size_cube{0};
        uint32_t max_array_layers{0};
        // Colour attachments one render target may carry (at least
        // @c max_color_attachments on supported hardware).
        uint32_t max_color_attachments{0};
        // Required alignment of a dynamic uniform / storage buffer
        // offset, for callers that sub-allocate one buffer.
        uint32_t uniform_buffer_offset_alignment{0};
        uint32_t storage_buffer_offset_alignment{0};
        // Multisample counts a colour / depth attachment may use (see
        // @ref sample_count_supported).
        sample_count_mask color_sample_counts{1};
        sample_count_mask depth_sample_counts{1};
        // Largest @c sampler_descriptor::max_anisotropy honoured.
        float max_anisotropy{1.0f};
        // Nanoseconds per timestamp tick (@c resolve_queries returns
        // ticks); 1.0 when timestamps are in nanoseconds already.
        float timestamp_period_ns{1.0f};
        uint32_t max_compute_workgroup_count[3]{0, 0, 0};
        uint32_t max_compute_workgroup_invocations{0};
        // Bytes of push constants a pipeline may declare (Vulkan's
        // maxPushConstantsSize); 0 on a device without
        // @c device_features::push_constants.
        uint32_t max_push_constants_size{0};
    };

    // Timestamp queries a frame writes and later reads back. A set is
    // a fixed number of slots; @c command_encoder::write_timestamp
    // fills one, @c device::resolve_queries reads them.
    struct query_set_descriptor
    {
        uint32_t count{0};
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

        // What this device can do and how much of it, valid after
        // @ref init. See @ref device_features / @ref device_limits.
        const device_features& features() const noexcept
        {
            return m_features;
        }

        const device_limits& limits() const noexcept
        {
            return m_limits;
        }

        // The uses (@ref texture_usage bits) a texture of @p format
        // supports on this device with optimal tiling: which formats
        // may be attached, sampled, written as storage images or
        // copied. Valid after @ref init.
        virtual texture_usage format_support(texture_format format) const = 0;

        // -- Resource creation --------------------------------------------

        virtual buffer create_buffer(const buffer_descriptor& descriptor) = 0;
        virtual texture create_texture(const texture_descriptor& descriptor) = 0;
        virtual sampler create_sampler(const sampler_descriptor& descriptor) = 0;
        virtual shader_module create_shader_module(const shader_module_descriptor& descriptor) = 0;
        virtual bind_group_layout create_bind_group_layout(const bind_group_layout_descriptor& descriptor) = 0;
        virtual pipeline create_pipeline(const pipeline_descriptor& descriptor) = 0;
        virtual pipeline create_compute_pipeline(const compute_pipeline_descriptor& descriptor) = 0;
        virtual bind_group create_bind_group(const bind_group_descriptor& descriptor) = 0;

        // A set of timestamp queries (see @ref query_set_descriptor).
        // Returns an invalid handle, with the reason logged, on a
        // device without @c device_features::timestamp_queries.
        virtual query_set create_query_set(const query_set_descriptor& descriptor) = 0;

        // -- Resource destruction -----------------------------------------

        virtual void destroy(buffer handle) = 0;
        virtual void destroy(texture handle) = 0;
        virtual void destroy(sampler handle) = 0;
        virtual void destroy(shader_module handle) = 0;
        virtual void destroy(bind_group_layout handle) = 0;
        virtual void destroy(pipeline handle) = 0;
        virtual void destroy(bind_group handle) = 0;
        virtual void destroy(query_set handle) = 0;

        // -- Debug names --------------------------------------------------

        // Label a resource for graphics debuggers and the driver's
        // debug output (@c glObjectLabel; @c vkSetDebugUtilsObjectNameEXT
        // when @c VK_EXT_debug_utils is enabled). No-ops by default and
        // on a device without @c device_features::debug_labels; a
        // backend overrides the ones it can label.
        virtual void set_debug_name(buffer handle, const char* name);
        virtual void set_debug_name(texture handle, const char* name);
        virtual void set_debug_name(sampler handle, const char* name);
        virtual void set_debug_name(pipeline handle, const char* name);
        virtual void set_debug_name(render_target handle, const char* name);

        // -- Resource updates ---------------------------------------------

        // Overwrite a region of @p buffer with @p size bytes from @p
        // data, starting at @p offset. Every later read of the buffer
        // by the GPU — in this frame or a later one — sees the new
        // bytes, and a frame still executing keeps the ones it was
        // recorded against: a backend with several frames in flight
        // keeps one copy of a @c dynamic_data buffer per frame slot and
        // carries writes across the copies as each slot comes round, so
        // a buffer written once (a material parameter block) and one
        // rewritten every frame (a per-frame UBO, an instance stream)
        // both behave as a single buffer would. A @c static_data buffer
        // is written through a staging copy that is queued ahead of the
        // frame; a @c stream_data buffer is written in place (see
        // @c buffer_usage_hint).
        virtual void write_buffer(buffer buffer_handle, const void* data, size_t size, size_t offset = 0) = 0;

        // Upload pixel data for a 2D texture. @p data is expected to
        // match the @c format the texture was created with: tightly
        // packed rows of @c width texels covering the whole base level,
        // so @p size must be at least @c width * @c height * texel
        // bytes — a backend rejects (and logs) a short upload rather
        // than reading past @p data. A block-compressed texture takes
        // whole blocks (@ref texture_image_bytes). Required before the
        // texture is sampled.
        virtual void write_texture(texture texture_handle, const void* data, size_t size) = 0;

        // Upload @p size bytes of tightly packed texels into @p region
        // of a 2D or 2D-array texture — one mip level and one layer,
        // at an (x, y) offset, of the given extent — for sub-rect
        // updates (atlas glyphs, streamed tiles), hand-authored mip
        // levels and array layers. @p data is laid out like
        // @ref write_texture over @c region.width by @c region.height
        // texels. Returns false and uploads nothing when the region
        // does not fit the level / layer or @p size is too small.
        virtual bool write_texture_region(texture texture_handle,
                                          const texture_write_region& region,
                                          const void* data,
                                          size_t size) = 0;

        // Synchronous readback of @p region of @p texture_handle into
        // @p out as tightly packed texels of the texture's format
        // (@ref texture_region_bytes gives the size). Waits for every
        // submitted command that wrote the texture to complete, so it
        // is for tests, screenshots and tooling, not the frame loop:
        // a per-frame readback goes through
        // @c command_encoder::copy_texture_to_buffer into a host-visible
        // buffer instead. Returns false, with the reason logged, when
        // the region does not fit, @p size is too small, or the texture
        // was not created with @c texture_usage_copy_src.
        virtual bool
        read_texture(texture texture_handle, const texture_copy_region& region, void* out, size_t size) = 0;

        // Upload pixel data for a 3D texture. @p data lays out the
        // full volume in slice-major order: each z slice is a 2D
        // image of @c width * @c height texels.
        virtual void write_texture_3d(texture texture_handle, const void* data, size_t size) = 0;

        // Upload pixel data for one face of a cube-map texture.
        virtual void write_cube_face(texture texture_handle, cube_face face, const void* data, size_t size) = 0;

        // Generate the full mip chain for a previously uploaded
        // texture. The texture must have been created with
        // @c texture_descriptor::mipmaps == @c true, in a format that is
        // not block-compressed (those upload every level instead).
        virtual void generate_mipmaps(texture texture_handle) = 0;

        // -- Render targets -----------------------------------------------

        // Handle for the backbuffer / default framebuffer that the
        // window presents.
        virtual render_target swapchain_target() = 0;

        // Notify the device that the window backbuffer was resized —
        // the renderer's window_resized listener is the caller, with
        // the drawable's pixel size, and renderer::init makes the same
        // call once. The swapchain target's recorded extent updates so
        // the next pass viewport defaults match the window. A zero
        // extent means the window is minimised: a backend that owns
        // presentation stops presenting (see @ref swapchain_suspended)
        // until a later non-zero resize, or its own surface poll, gives
        // it a size to rebuild against.
        virtual void resize_swapchain(uint32_t width, uint32_t height) = 0;

        // True while the backend has no presentable swapchain: the
        // surface reports no extent, or the last rebuild failed and is
        // retried at the next frame top. The main loop skips whole
        // frames while window::is_minimized(), so this is the device's
        // own state for what still reaches it — a surface that lags the
        // window, an out-of-date recovery that found no extent, a
        // failed rebuild. While set, a frame that does run executes its
        // off-screen passes but every pass that targets the swapchain
        // records nothing. Backends that present through the window
        // (OpenGL) never suspend.
        virtual bool swapchain_suspended() const
        {
            return false;
        }

        // Allocate an off-screen render target: zero or more colour
        // attachments and an optional depth attachment, as
        // @ref render_target_descriptor lays out. Every attachment the
        // descriptor does not import is allocated by the device as a
        // sampled, single-mip texture of the target's shape and
        // released together with the target; an imported texture is
        // attached at the level / layer named and left to its owner.
        // The attachments are exposed via @ref render_target_color_texture
        // and @ref render_target_depth_texture so the next pass in the
        // chain can sample them. Returns an invalid handle, with the
        // reason logged, when @ref validate_render_target_descriptor
        // rejects the descriptor or an imported texture does not fit.
        virtual render_target create_render_target(const render_target_descriptor& descriptor) = 0;

        // Release a previously created off-screen render target and
        // its owned attachments. No-op for the swapchain handle.
        virtual void destroy(render_target handle) = 0;

        // Texture handle of colour attachment @p index of @p handle
        // (the texture the attachment was allocated as, or the imported
        // one), or an invalid handle for the swapchain or an index past
        // the target's colour attachments. Used by post-process passes
        // to bind the previous pass's output as a sampled input.
        virtual texture render_target_color_texture(render_target handle, uint32_t index = 0) = 0;

        // Texture handle of the depth attachment of @p handle, or an
        // invalid handle for the swapchain or a target created without
        // depth. The depth texture is allocated as a sampled texture so
        // a later pass can read it — the shadow passes render scene
        // depth into depth-only @c depth32_float targets and the lit
        // materials sample it.
        virtual texture render_target_depth_texture(render_target handle) = 0;

        // -- Queries ------------------------------------------------------

        // Read @p count timestamps of @p set from @p first into
        // @p out_ticks (in @c device_limits::timestamp_period_ns units)
        // without waiting. Returns false, leaving @p out_ticks alone,
        // when any of them has not completed yet — a caller keeps the
        // previous frame's values and retries next frame — or when the
        // device has no timestamp support.
        virtual bool resolve_queries(query_set set, uint32_t first, uint32_t count, uint64_t* out_ticks) = 0;

        // -- Frame boundary -----------------------------------------------

        // Frames the backend may have in flight at once: how many
        // frames' command buffers can be executing or queued while the
        // renderer records the next. 1 on an immediate-mode backend
        // (OpenGL) and on a deferred backend configured for a single
        // frame; the Vulkan backend reads
        // @c core::graphics_settings::frames_in_flight at init. Fixed
        // for the device's lifetime.
        virtual uint32_t frames_in_flight() const noexcept
        {
            return 1;
        }

        // The slot, in [0, @ref frames_in_flight), the current frame
        // records into; it advances at @ref end_frame. A frame's slot
        // is only reused once that frame's GPU work has retired, so a
        // caller that keeps one copy of a per-frame resource per slot
        // (the per-draw ring's regions) never rewrites a copy the GPU
        // may still read. Host-visible buffers created with
        // @c buffer_usage_hint::dynamic_data need no such care: the
        // backend keeps a copy per slot itself (see
        // @ref write_buffer).
        virtual uint32_t frame_slot() const noexcept
        {
            return 0;
        }

        // Open a frame. The renderer calls this once per rendered
        // frame, before it creates the frame's command encoder and
        // before any per-frame host write (UBO uploads, bind-group
        // rebuilds, buffer re-uploads) for that frame. A backend that
        // defers execution blocks here until the GPU work of the frame
        // that last used this frame's slot has finished (the previous
        // frame at one frame in flight) and then frees the resources
        // whose destruction it deferred while that work could still
        // reference them — so host writes never race a device read
        // and nothing is freed out from under a command buffer that
        // is being recorded. Immediate-mode backends (OpenGL) treat it
        // as a no-op.
        virtual void begin_frame() = 0;

        // Close the frame opened by @ref begin_frame, after the frame's
        // encoder has been submitted. A backend that owns presentation
        // (Vulkan) presents the swapchain image it acquired for this
        // frame here and rolls its per-frame bookkeeping; OpenGL
        // presents through @c window::swap_buffers and treats this as
        // a no-op. Work submitted outside a begin_frame / end_frame
        // bracket (start-up uploads, the IBL prefilter) needs no
        // bracket: the backend queues it and the next begin_frame
        // waits for it like a frame, so resources it referenced may be
        // destroyed right after the submit.
        virtual void end_frame() = 0;

        // -- Command recording --------------------------------------------

        // Allocate a new command encoder. Each encoder records one
        // or more render passes; submission is implicit on the
        // OpenGL backend (drawing happens immediately as it's
        // recorded), while the Vulkan backend records into a command
        // buffer and defers execution until @ref submit.
        virtual std::unique_ptr<command_encoder> create_command_encoder() = 0;

        // Submit the encoder's recorded work for execution. After
        // this call the encoder is consumed.
        virtual void submit(std::unique_ptr<command_encoder> encoder) = 0;

#if defined(_DEBUG)
        // -- Shader hot reload (debug builds) ------------------------------

        // Swap @c spirv in behind each live module of @p updates, keeping
        // every handle, and rebuild every pipeline created from one of
        // them, so the draws and dispatches recorded afterwards run the
        // new code while the callers keep the handles they hold. All or
        // nothing: when a new stage fails to create or a dependent
        // pipeline fails to rebuild, the reason is logged, nothing
        // changes and false is returned. A module whose handle is no
        // longer live is skipped. Call between frames; the replaced
        // backend objects may still be referenced by frames in flight
        // and are released through the backend's deferred destroy. The
        // default (a device without hot reload) changes nothing and
        // returns false.
        virtual bool reload_shader_modules(const std::vector<shader_module_update>& updates);

        // Whether @p module still names a live shader module on this
        // device, so a registry of modules can drop the ones their owner
        // has destroyed. False by default.
        virtual bool shader_module_live(shader_module module);
#endif

    protected:
        // Filled by the backend in @c init; see @ref features /
        // @ref limits.
        device_features m_features{};
        device_limits m_limits{};
    };

    // Construct a concrete device for the requested backend. The
    // returned device is in a not-yet-initialised state — the caller
    // must invoke @c init() once the window/GL context is live.
    std::unique_ptr<device> create_device(backend_type type);
} // namespace rendering_engine::gpu
