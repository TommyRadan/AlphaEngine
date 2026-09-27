// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device.hpp
 * @brief Vulkan implementation of @ref gpu::device.
 *
 * The @c vk_device class is declared in full and member functions are
 * split across translation units by resource family (vk_device.cpp for
 * lifecycle / capabilities / swapchain / render targets / queries /
 * encoders / lookup_*, vk_device_buffer.cpp for buffers, etc.).
 *
 * The backend keeps up to k_max_frames_in_flight frames in flight
 * (core::graphics_settings::frames_in_flight, 2 by default): each
 * frame records into its own slot — an image-available semaphore, an
 * in-flight fence, a command pool and a swapchain depth image — and
 * begin_frame waits only for the frame that last used the slot, so the
 * CPU records frame N+1 while the GPU draws frame N. Everything the
 * host rewrites is kept apart per slot: a dynamic_data buffer holds one
 * copy per slot and a bind group over one holds one descriptor set per
 * slot (see vk_buffer::region_count), so no host write ever lands in
 * memory a frame in flight reads. Resources are destroyed through a
 * queue gated on the queue submission that could last have referenced
 * them (see enqueue_destroy). Shaders are runtime SPIR-V via
 * @ref gpu::compile_glsl_to_spirv.
 * Recording is single-threaded except inside a render pass begun with
 * render_pass_descriptor::parallel: its draws go into secondary
 * command buffers, one per recording lane (acquire_secondary_command_
 * buffer: a command pool per lane and frame slot, reset with the
 * frame's primary pool), which the job pool's workers record at once
 * while the main thread waits, and the primary then executes in order.
 * The two device paths a worker can reach — the lazy per-render-pass
 * VkPipeline build (graphics_pipeline_for) and the multi-buffered
 * region sync a bind performs (ensure_host_region_current) — are
 * serialised by a mutex each; every other lookup is a read of state
 * the main thread leaves alone for the duration of the fork. Device
 * memory comes from the
 * Vulkan Memory Allocator (vk_allocator.hpp): every buffer and image is
 * a sub-allocation of VMA's memory blocks rather than its own
 * vkAllocateMemory, and host-visible buffers are persistently mapped
 * by their allocation. Uploads go through one persistently mapped
 * staging ring and a batched transfer command buffer that is submitted
 * with its own fence ahead of the frame (see stage_upload). Compute
 * pipelines and storage-image bind groups are implemented (the IBL
 * convolution runs on the GPU), as are indirect draws,
 * memory barriers, the buffer <-> texture copies, timestamp queries
 * and the VK_EXT_debug_utils labels.
 */

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/handle_pool.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_allocator.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_staging_ring.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    // Best-effort VkResult → human-readable string, used in error
    // logs so the user can pinpoint a failing call without validation
    // layers.
    const char* vk_result_to_string(VkResult r);

    // True for VK_SUCCESS. Anything else logs "<what> failed: <result>"
    // at error level and returns false, so a call site reads
    // `if (!vk_check(vkFoo(...), "vkFoo")) { bail; }` instead of
    // discarding the result. Only for calls whose every non-success
    // code is a failure; a call with informational codes
    // (VK_SUBOPTIMAL_KHR, VK_INCOMPLETE) inspects its result itself.
    bool vk_check(VkResult result, const char* what);

    // The optional core features the backend asks for are requested
    // only when vkGetPhysicalDeviceFeatures reports them, and the
    // grants land in the base class's device_features (see
    // gpu::device::features): a feature is requested only when the
    // physical device reports it, and what is missing is logged once
    // at warning level. Consumers gate on the grants rather than
    // assuming: a wireframe material falls back to filled polygons
    // without fill_mode_non_solid, multi-draw indirect is unrolled
    // into single draws without multi_draw_indirect, and a pipeline
    // that attaches a stage whose feature is missing is refused.
    struct vk_device : public device
    {
        vk_device();
        ~vk_device() override;

        void init() override;
        void quit() override;

        texture_usage format_support(texture_format format) const override;

        buffer create_buffer(const buffer_descriptor& descriptor) override;
        texture create_texture(const texture_descriptor& descriptor) override;
        sampler create_sampler(const sampler_descriptor& descriptor) override;
        shader_module create_shader_module(const shader_module_descriptor& descriptor) override;
        bind_group_layout create_bind_group_layout(const bind_group_layout_descriptor& descriptor) override;
        pipeline create_pipeline(const pipeline_descriptor& descriptor) override;
        pipeline create_compute_pipeline(const compute_pipeline_descriptor& descriptor) override;
        bind_group create_bind_group(const bind_group_descriptor& descriptor) override;
        query_set create_query_set(const query_set_descriptor& descriptor) override;

        void destroy(buffer handle) override;
        void destroy(texture handle) override;
        void destroy(sampler handle) override;
        void destroy(shader_module handle) override;
        void destroy(bind_group_layout handle) override;
        void destroy(pipeline handle) override;
        void destroy(bind_group handle) override;
        void destroy(query_set handle) override;

        void set_debug_name(buffer handle, const char* name) override;
        void set_debug_name(texture handle, const char* name) override;
        void set_debug_name(sampler handle, const char* name) override;
        void set_debug_name(pipeline handle, const char* name) override;
        void set_debug_name(render_target handle, const char* name) override;

        void write_buffer(buffer buffer_handle, const void* data, size_t size, size_t offset) override;
        void write_texture(texture texture_handle, const void* data, size_t size) override;
        bool write_texture_region(texture texture_handle,
                                  const texture_write_region& region,
                                  const void* data,
                                  size_t size) override;
        bool read_texture(texture texture_handle, const texture_copy_region& region, void* out, size_t size) override;
        void write_texture_3d(texture texture_handle, const void* data, size_t size) override;
        void write_cube_face(texture texture_handle, cube_face face, const void* data, size_t size) override;
        void generate_mipmaps(texture texture_handle) override;

        render_target swapchain_target() override;
        // Window-size hint. A size that matches the live swapchain is
        // a no-op; 0x0 suspends presentation (minimised); anything
        // else rebuilds the swapchain against the surface's *current*
        // capabilities — the surface, not the hint, decides the
        // extent (see recreate_swapchain).
        void resize_swapchain(uint32_t width, uint32_t height) override;
        bool swapchain_suspended() const noexcept override;
        render_target create_render_target(const render_target_descriptor& descriptor) override;
        void destroy(render_target handle) override;
        texture render_target_color_texture(render_target handle, uint32_t index = 0) override;
        texture render_target_depth_texture(render_target handle) override;

        bool resolve_queries(query_set set, uint32_t first, uint32_t count, uint64_t* out_ticks) override;

        std::unique_ptr<command_encoder> create_command_encoder() override;
        // Queue the encoder's command buffer, after flushing the open
        // transfer batch ahead of it so every upload recorded so far
        // lands first in queue order. Inside a frame that has acquired
        // a swapchain image the submission waits the slot's
        // image-available semaphore, signals that image's
        // render-finished semaphore and the slot's in-flight fence, and
        // leaves the present to end_frame. Outside a frame (or in a
        // frame whose passes never reached the swapchain) the work is
        // queued on the current slot's fence alone — no semaphores, no
        // present, no idle wait — and the next begin_frame of that slot
        // waits for it like a frame.
        void submit(std::unique_ptr<command_encoder> encoder) override;

        // Frame boundary. begin_frame waits the in-flight fence of the
        // frame's slot — armed by the frame that last recorded into it,
        // frames_in_flight() frames ago — reclaims every transfer batch
        // that has completed, resets the slot's command pool and drains
        // the deferred destroys whose submission has retired, so every
        // host write the renderer makes afterwards lands in memory the
        // GPU is done with. It does not acquire an image; while the
        // swapchain is suspended it polls the surface and rebuilds as
        // soon as the extent is usable again. end_frame presents the
        // image acquired this frame (when submit queued work against
        // it), rebuilds the swapchain if the present reported it out of
        // date or suboptimal, and advances to the next slot.
        void begin_frame() override;
        void end_frame() override;

        // The slot ring: see gpu::device. The count is read from the
        // settings at init and clamped to k_max_frames_in_flight.
        uint32_t frames_in_flight() const noexcept override;
        uint32_t frame_slot() const noexcept override;

#if defined(_DEBUG)
        // Debug hot reload (see gpu::device). The new VkShaderModules are
        // created first, then every pipeline built from a replaced module
        // is rebuilt against them — the compute object, and each graphics
        // variant for the render pass, orientation and attachments it was
        // built for — before anything is installed, so a failure puts the
        // old modules back and leaves every pipeline as it was. The old
        // modules and pipeline objects go through enqueue_destroy.
        bool reload_shader_modules(const std::vector<shader_module_update>& updates) override;
        bool shader_module_live(shader_module module) override;
#endif

        // Internal accessors used by the encoder to map handles
        // back to records. Definitions in vk_device.cpp.
        vk_buffer* lookup_buffer(buffer h);
        vk_texture* lookup_texture(texture h);
        vk_sampler* lookup_sampler(sampler h);
        vk_shader_module* lookup_shader_module(shader_module h);
        vk_pipeline* lookup_pipeline(pipeline h);
        vk_bind_group* lookup_bind_group(bind_group h);
        vk_render_target* lookup_render_target(render_target h);
        vk_bind_group_layout* lookup_bind_group_layout(bind_group_layout h);
        vk_query_set* lookup_query_set(query_set h);

        // Vulkan handles + helpers exposed to per-resource TUs.
        VkInstance instance() const noexcept;
        VkDevice vk_handle() const noexcept;
        VkPhysicalDevice physical_device() const noexcept;
        VkQueue graphics_queue() const noexcept;
        uint32_t graphics_queue_family() const noexcept;
        // The memory allocator every buffer and image is allocated
        // from; alive from create_logical_device until quit.
        VmaAllocator allocator() const noexcept;
        // The pipeline cache every graphics and compute pipeline is
        // created through — the backend's own and the Dear ImGui
        // backend's. Seeded at init from the file the previous run
        // wrote (see vk_pipeline_cache.hpp) and written back at quit;
        // VK_NULL_HANDLE when the shader cache is disabled or the cache
        // could not be created, which every vkCreate*Pipelines call
        // accepts.
        VkPipelineCache pipeline_cache() const noexcept;
        // True once a queue operation reported VK_ERROR_DEVICE_LOST.
        // From then on every submit, acquire and present is a no-op
        // and the next end_frame throws, once, so the main loop's
        // failure path tears the engine down in order rather than
        // looping on a dead device. See mark_device_lost.
        bool device_lost() const noexcept;
        // The VkFormat backing @p format on this device. Depth formats
        // come from the fallback chains resolved once at init
        // (resolve_depth_formats, see vk_negotiate.hpp); colour formats
        // are the fixed translation. Both the swapchain depth buffer
        // and create_texture go through here so a target and the
        // textures sampled from it agree on the format.
        VkFormat vk_format_for(texture_format format) const noexcept;
        // Number of images the swapchain was created with — surfaced so
        // the Dear ImGui Vulkan backend can size its frame resources.
        uint32_t swapchain_image_count() const noexcept;
        // Bumped on every successful swapchain (re)build. Anything that
        // baked a swapchain render pass into its own objects — the
        // Dear ImGui Vulkan backend builds its pipeline against one —
        // compares this before recording and rebuilds when it moved;
        // the render passes it knew are retired by then.
        uint64_t swapchain_generation() const noexcept;
        uint32_t current_swapchain_image_index() const noexcept;
        bool have_current_swapchain_image() const noexcept;
        // Index into a swapchain variant's framebuffers of the one
        // that attaches swapchain image @p image_index together with
        // the current frame slot's depth image (one per slot, so two
        // frames in flight never share a depth buffer).
        uint32_t swapchain_framebuffer_index(uint32_t image_index) const noexcept;
        bool depth_clip_control_enabled() const noexcept;
        // True when VK_EXT_extended_dynamic_state is enabled. Lets
        // the encoder fall back to vkCmdBindVertexBuffers when the
        // extension is missing; without dynamic stride, materials
        // declared with @c vertex_layout.stride==0 (the engine's
        // shorthand for "stride supplied per draw") would bake
        // stride 0 into the pipeline and collapse the mesh to a
        // single point. The encoder uses @c vkCmdBindVertexBuffers2EXT
        // when the flag is on.
        bool extended_dynamic_state_enabled() const noexcept;
        PFN_vkCmdBindVertexBuffers2EXT cmd_bind_vertex_buffers2() const noexcept;
        // The VK_EXT_debug_utils label entry points, null when the
        // extension was not enabled (the encoder then records no
        // labels).
        PFN_vkCmdBeginDebugUtilsLabelEXT cmd_begin_debug_label() const noexcept;
        PFN_vkCmdEndDebugUtilsLabelEXT cmd_end_debug_label() const noexcept;

        // Acquire (or lazily build) the render pass + framebuffers of
        // @p target for @p key: the per-attachment load / store ops
        // and whether depth takes part. Entries of the key past the
        // target's colour attachment count are ignored.
        VkRenderPass acquire_render_pass(vk_render_target& target, const vk_render_pass_key& key);

        // The single-colour convenience: every colour attachment loads
        // with @p color_load and stores, depth loads with @p depth_load
        // and stores. What the debug overlay asks for to match the
        // debug pass.
        VkRenderPass acquire_render_pass(vk_render_target& target,
                                         VkAttachmentLoadOp color_load,
                                         VkAttachmentLoadOp depth_load,
                                         bool use_depth);

        // Look up — or lazily build — the @c VkPipeline for
        // @p handle that is compatible with @p render_pass. The
        // engine creates pipelines once but binds them across
        // render passes with different attachment formats (the
        // off-screen scene target vs. the swapchain), so each
        // pipeline_descriptor materialises into one VkPipeline per
        // render pass it draws against. The cache is owned by the
        // pipeline record and torn down with it; the entries built
        // against a render pass are purged when that pass is retired
        // (see retire_render_pass_variants), which is why the key
        // carries @p render_pass_generation and not the handle alone.
        // @p y_flipped selects the front-face mapping: swapchain
        // passes render through a negative-height viewport (CCW
        // → CW), off-screen passes don't (CCW stays CCW).
        // @p color_count and @p samples describe the pass's
        // attachments, which the blend and multisample state must
        // match.
        VkPipeline graphics_pipeline_for(pipeline handle,
                                         VkRenderPass render_pass,
                                         uint64_t render_pass_generation,
                                         bool y_flipped,
                                         uint32_t color_count,
                                         VkSampleCountFlagBits samples);

        // Lazily create (and cache on the texture) the single-level,
        // single-layer 2D view a framebuffer attaches @p tex through
        // at @p mip / @p layer, carrying every aspect of the format.
        // Returns VK_NULL_HANDLE when the subresource is out of range
        // or the view cannot be created.
        VkImageView attachment_image_view(vk_texture& tex, uint32_t mip, uint32_t layer);

        // Record a whole-image layout transition of @p tex into
        // @p cmd and update the tracked layout. No-op when the image
        // is already in @p new_layout. Used by the encoder's copies,
        // which need the transfer layouts around a copy and the
        // resting layout back afterwards.
        void record_layout_transition(VkCommandBuffer cmd, vk_texture& tex, VkImageLayout new_layout);

        // Acquire the next swapchain image for the current frame.
        // Called lazily by the render-pass encoder when the first
        // swapchain-targeted pass opens, so a frame that never reaches
        // the swapchain does not acquire one. One attempt per frame:
        // the frame's later swapchain passes see the same outcome, so
        // a frame either reaches the swapchain in every pass or in
        // none. Must run inside a begin_frame / end_frame bracket.
        // An out-of-date acquire rebuilds the swapchain against the
        // surface's current extent and retries once, so the frame
        // lands in the new swapchain; a rebuild that finds no usable
        // extent (minimised) suspends instead and the frame skips the
        // swapchain. Because a rebuild retires the swapchain target's
        // render-pass variants, callers read nothing from that target
        // until this returns. The slot's fence is not touched here —
        // submit resets it right before the queue submission that
        // signals it, so a failed acquire never leaves it unsignaled
        // for the next begin_frame to block on. An image handed back
        // while the frame that last drew it is still in flight waits
        // that frame's fence (see m_image_last_slot).
        void acquire_swapchain_image();

        // -- Uploads ------------------------------------------------------
        //
        // Every upload (buffer initial data, write_buffer to device-local
        // memory, texture uploads, mipmap generation, the initial layout
        // transition of a new image) is recorded into the open transfer
        // batch: one primary command buffer with its own fence. The
        // batch is submitted ahead of the frame's command buffer in
        // submit(), or earlier when the staging ring runs out of space.
        // It opens with an all-commands -> transfer barrier, so its
        // copies run behind whatever the queue was still executing
        // (the previous frame, for a flush between frames), and ends
        // with a transfer -> all-commands memory barrier, so whatever
        // the frame reads after it in queue order sees the uploads.
        // Nothing here waits the queue idle: the ring space and
        // the dedicated staging buffers of a batch are reclaimed when
        // its fence signals (begin_frame, or a ring-full wait for the
        // oldest batch). The CPU-side image layout each vk_texture
        // records is its layout in batch order; a compute pass moves a
        // storage image to GENERAL inside the frame's command buffer and
        // back before it ends, so no upload may target a texture bound
        // by a compute pass that is still open.

        // The open batch's command buffer, begun on first use. Returns
        // VK_NULL_HANDLE (callers skip their upload, which is logged)
        // when no batch can be begun or the device is lost.
        VkCommandBuffer transfer_command_buffer();

        // Source of a staged copy: @p offset bytes into @p buffer hold
        // the caller's data, and @p cmd is the batch to record the copy
        // into. Both are valid until the batch is flushed, which nothing
        // does between stage_upload and the copy that follows it.
        struct staged_upload
        {
            VkBuffer buffer{VK_NULL_HANDLE};
            VkDeviceSize offset{0};
            VkCommandBuffer cmd{VK_NULL_HANDLE};
        };

        // Copy @p size bytes of @p data into staging memory for the open
        // batch: the ring when the upload fits (a full ring flushes the
        // open batch and waits for the oldest submitted one, which is
        // the only wait on the upload path), a dedicated host-visible
        // buffer released with the batch when it does not. The ring
        // offset is aligned for any vkCmdCopyBufferToImage texel block
        // the engine's formats have. Returns false, with the reason
        // logged, when nothing could be staged.
        bool stage_upload(const void* data, size_t size, staged_upload& out);

        // Record the copy of a staged upload into @p dst at
        // @p dst_offset, with the transfer -> transfer barrier a second
        // copy into the same buffer within one batch needs.
        void record_buffer_copy(const staged_upload& source, VkBuffer dst, VkDeviceSize dst_offset, VkDeviceSize size);

        // End and queue the open transfer batch on its fence; a batch
        // that recorded nothing is returned to the pool unsubmitted.
        // Returns false when the submission failed (the uploads it
        // carried are lost, logged as an error) — the batch is still
        // retired in order so the ring stays consistent.
        bool flush_transfer_batch();

        // A primary command buffer for a command encoder, from the
        // current frame's pool. Buffers are handed out in order and
        // reclaimed together when begin_frame resets the pool after the
        // fence wait, so nothing is allocated or freed per frame in the
        // steady state. Returns VK_NULL_HANDLE when the device is lost
        // or the allocation failed (logged).
        VkCommandBuffer acquire_frame_command_buffer();

        // -- Multi-buffered host-visible buffers ---------------------------
        //
        // See vk_buffer::region_count. A dynamic_data buffer's writes and
        // binds all go to the current frame slot's region; these bring a
        // region up to date before the GPU or the host touches it.

        // Byte offset of the current frame slot's region within
        // @p record's VkBuffer: what every bind and copy adds to the
        // caller's offset. 0 for a single-region buffer.
        VkDeviceSize host_region_offset(const vk_buffer& record) const noexcept;

        // Copy across whatever the current slot's region of @p record
        // has missed since it was last written, so a bind of it reads
        // the buffer's latest contents. Called by the encoder for every
        // buffer it binds or copies from; a no-op for a single-region
        // buffer or a region that is up to date. Outside a frame the
        // slot's fence is waited first, since the frame that last used
        // the slot may still be reading the region.
        void ensure_host_region_current(vk_buffer& record);

        // ensure_host_region_current for every buffer @p group binds.
        void prepare_bind_group(vk_bind_group& group);

        // Allocate one descriptor set of @p layout from the pool chain:
        // the newest pool first, and when it is exhausted
        // (VK_ERROR_OUT_OF_POOL_MEMORY / VK_ERROR_FRAGMENTED_POOL) a
        // fresh, larger pool is appended and the allocation retried
        // once. @p out_pool receives the pool the set came from, which
        // is the only pool it may be freed back to. Returns false, with
        // an error logged, when even the fresh pool refuses.
        bool
        allocate_descriptor_set(VkDescriptorSetLayout layout, VkDescriptorSet& out_set, VkDescriptorPool& out_pool);

        // Lazily create (and cache on the texture) the single-mip image
        // view used to bind @p tex as a storage image at @p level. Cube,
        // array and 3D textures bind every layer through one view; the
        // level selects the subresource. Returns VK_NULL_HANDLE if the
        // level is out of range or the view cannot be created.
        VkImageView storage_image_view(vk_texture& tex, uint32_t level);

        // Record a layout transition for a storage-capable texture into
        // @p cmd: to VK_IMAGE_LAYOUT_GENERAL for compute writes when
        // @p to_general is true, otherwise back to the sampled layout.
        // Updates the tracked layout and returns true only when a
        // transition was actually recorded (the image was not already
        // in the requested layout), so callers can de-duplicate the
        // restore at compute-pass end.
        bool transition_storage_image(VkCommandBuffer cmd, texture handle, bool to_general);

        // Per-frame draw counters surfaced as a one-shot log for the
        // first few frames so a missing draw call is visible without
        // attaching RenderDoc. Cleared in end_frame. Main thread only:
        // a secondary encoder tallies its own draws and the primary
        // merges them through note_draws when it executes the secondary.
        void note_render_pass_opened(bool is_swapchain, bool use_depth);
        void note_draw(uint32_t vertex_count);
        void note_draw_indexed(uint32_t index_count);
        void note_draws(uint32_t draws, uint32_t vertices, uint32_t draws_indexed, uint32_t indices);

        // A secondary command buffer for a parallel render pass's chunk,
        // from recording lane @p lane's pool of the current frame slot
        // (the pool and the lane itself are created on first use). Like
        // the primary buffers, lanes hand their buffers out in order and
        // take them all back with the pool reset at the slot's next
        // begin_frame. Main thread only, before the fork: the lane's
        // pool is then the recording thread's alone until the join.
        // Returns VK_NULL_HANDLE when the device is lost or the pool or
        // buffer could not be created (logged).
        VkCommandBuffer acquire_secondary_command_buffer(uint32_t lane);

        // Destroy callbacks queued from @c destroy() overloads. Freeing
        // a buffer or descriptor set while a command buffer that
        // references it is executing, or still being recorded, is
        // invalid (VUID-vkDestroyBuffer-buffer-00922 /
        // VUID-vkFreeDescriptorSets-pDescriptorSets-00309) — the
        // engine's buffer / bind-group churn would otherwise
        // trigger streams of it — and with several frames in flight
        // the previous frame's command buffer is still running when a
        // resource is destroyed. Each
        // @c destroy() pushes a closure here stamped with the serial of
        // the last queue submission that can reference the resource:
        // inside a frame that is the frame's own submission, still to
        // come; outside a frame the most recent one, since nothing is
        // being recorded. Every frame submission arms its slot's fence
        // with its serial, and waiting a fence marks its serial — and
        // every earlier one, which the queue completed first — retired.
        // @c drain_pending_destroys runs the entries whose serial has
        // retired, and only at points where no open command buffer can
        // reference them: in @c begin_frame, after the slot's fence
        // wait and before the renderer records anything (so a bind
        // group a material rebuilds mid-frame is never freed while the
        // open command buffer already references it); under
        // vkDeviceWaitIdle in @c quit; and under @c flush_pending_destroys
        // for a caller with the same problem outside this queue (the
        // ImGui Vulkan backend's own descriptor sets — see its use in
        // the editor overlay). A transfer batch may reference the resource as well
        // — a copy into a buffer or image destroyed before the batch ran
        // — so each entry also records the newest batch id at enqueue
        // time and runs only once every batch up to that id has retired.
        void enqueue_destroy(std::function<void()> fn);
        void drain_pending_destroys();

        // Wait the queue idle and flush every deferred destroy right
        // away, including one stamped for a submission that has not
        // happened yet: once idle, nothing can still reference it,
        // exactly as @c quit reasons about its own final drain. For a
        // caller that manages Vulkan objects of its own outside this
        // device (through @c enqueue_destroy, as the ImGui Vulkan
        // backend's descriptor-set cache does) and must reclaim every
        // one of them before tearing its own state down — ahead of
        // @c quit draining the same queue, by which point that state is
        // already gone. A no-op once the device is lost or was never
        // brought up.
        void flush_pending_destroys();

        // 1x1 placeholder textures (one 2D, one cube) bound in place of
        // an unset sampler slot. Vulkan requires every statically-used
        // descriptor to reference a valid resource, so create_bind_group
        // substitutes the matching-dimension default when a material
        // leaves a texture binding empty. Created in init(), released in
        // quit().
        texture default_texture(texture_dimension dim) const noexcept;

    private:
        void create_instance();
        void create_default_textures();
        void create_debug_messenger();
        void destroy_debug_messenger();
        // Resolve the VK_EXT_debug_utils label / object-name entry
        // points once the instance exists; leaves them null (and the
        // debug_labels feature off) when the extension is not enabled.
        void load_debug_utils_functions();
        void create_surface();
        void pick_physical_device();
        void create_logical_device();
        // Fill the base class's device_features / device_limits from
        // the physical device's properties and the grants recorded by
        // create_logical_device.
        void query_capabilities();
        // The VMA allocator over the logical device, told the API
        // version the instance and the physical device agree on.
        // Throws when VMA refuses; destroyed after every allocation.
        void create_allocator();
        void destroy_allocator();
        // The transfer pool the batches allocate from and the per-frame
        // pools the encoders draw on. Throws when a pool cannot be
        // created.
        void create_command_pools();
        void destroy_command_pools();
        // The persistently mapped staging ring (k_staging_ring_bytes).
        // Returns false, with the failure logged, when the buffer could
        // not be allocated or mapped; init treats that as fatal.
        bool create_staging_ring();
        void destroy_staging_ring();
        // Append one pool to the chain, sized by
        // descriptor_pool_budget_for(chain length). Returns false with
        // an error logged when the driver refuses.
        bool create_descriptor_pool();
        // Create m_pipeline_cache, seeded from the pipeline-cache file
        // of this GPU in the shader cache directory when that file is
        // intact and was written by this GPU and driver (logged either
        // way). With the shader cache disabled no cache is created.
        void create_pipeline_cache();
        // Write the cache's data back to its file (skipped when it did
        // not change since it was read, and after a device loss), then
        // destroy it. Runs in quit, with the device idle.
        void save_and_destroy_pipeline_cache();
        // A compute VkPipeline over @p module and @p layout, through the
        // pipeline cache; VK_NULL_HANDLE (logged) on failure.
        VkPipeline build_compute_pipeline(VkShaderModule module, VkPipelineLayout layout);
        // Resolve the three engine depth formats against
        // vkGetPhysicalDeviceFormatProperties through the fallback
        // chains in vk_negotiate.hpp, log the outcome, and throw when a
        // chain resolves to nothing (the spec mandates D32_SFLOAT, so
        // only a broken driver gets there).
        void resolve_depth_formats();
        // The sampler substituted for a texture whose own sampler
        // failed to create, so a null sampler never reaches a
        // combined-image-sampler descriptor.
        void create_fallback_sampler();
        // Record a device loss reported by @p what: logs once at fatal
        // level and flips m_device_lost. Every later submit / acquire /
        // present is a no-op and the next end_frame throws.
        void mark_device_lost(const char* what);
        // Result check for the queue-level calls that can report
        // VK_ERROR_DEVICE_LOST (submit, present, fence and idle waits):
        // that code goes through mark_device_lost, any other failure is
        // logged through vk_check. Returns true on VK_SUCCESS.
        bool check_queue_result(VkResult result, const char* what);
        // Name @p object_handle of @p type for debuggers and validation
        // messages through vkSetDebugUtilsObjectNameEXT; no-op without
        // the extension.
        void name_object(VkObjectType type, uint64_t object_handle, const char* name);
        // Build the swapchain for @p extent plus everything hanging off
        // it (image views, one depth buffer per frame slot, the
        // per-image render-finished semaphores). The previous swapchain, if any,
        // is handed over as oldSwapchain — which retires it whether or
        // not the call succeeds — and released here. Returns false,
        // with nothing left half-built, when any step fails; the caller
        // decides whether that is fatal (init) or a suspension (the
        // render loop).
        bool create_swapchain(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D extent);
        void destroy_swapchain();
        // Forced rebuild used by every recovery site: re-queries the
        // surface capabilities (never the cached window size — an
        // OS-driven out-of-date arrives without a resize), waits the
        // device idle, retires the swapchain target's render-pass
        // variants and builds a new swapchain at the surface's extent.
        // A 0x0 extent (minimised) or a failed build leaves the device
        // suspended instead of throwing; a later call resumes it.
        // Returns true when a new swapchain is live.
        bool recreate_swapchain();
        // Enter the suspended state, logging @p reason once per
        // suspension (at error level when @p is_error).
        void suspend_swapchain(const char* reason, bool is_error);
        // Retire every render-pass variant of @p target: its
        // VkRenderPass objects, the framebuffers built on them and —
        // by generation, walking the pipeline pool — every graphics
        // pipeline variant built against them, so no pipeline can be
        // matched to a recycled render-pass handle. Render passes and
        // pipelines go through the deferred-destroy queue (they may be
        // bound by the previous frame's command buffer when a target
        // is destroyed mid-run). With @p device_idle the caller has
        // waited the device idle and is about to destroy the image
        // views the framebuffers reference, so those are freed right
        // here, ahead of their attachments; otherwise they are
        // deferred with the rest.
        void retire_render_pass_variants(vk_render_target& target, bool device_idle);
        void create_sync_objects();
        void destroy_sync_objects();
        // Wait the in-flight fence of @p slot when a submission armed
        // it, disarm it, and mark the submission it covers (and every
        // earlier one) retired for the deferred destroys. Returns false
        // when the wait failed (the device is then lost).
        bool wait_slot_fence(uint32_t slot);
        // After vkDeviceWaitIdle: every fence is idle and every
        // submission has retired.
        void note_device_idle();
        // vkResetCommandPool on the current frame's pool, after the
        // fence wait proved every buffer from it complete, and rewind
        // its hand-out cursor.
        void reset_frame_command_pool();
        // Bring @p slot's region of @p record up to date except for
        // [@p skip_begin, @p skip_end), which the caller is about to
        // overwrite: copies the rest of the region's gap from the latest
        // region and clears the gap.
        void sync_host_region(vk_buffer& record, uint32_t slot, VkDeviceSize skip_begin, VkDeviceSize skip_end);
        // Before the host writes a multi-buffered region outside a
        // frame: the frame that last used the current slot may still
        // read it, so its fence is waited (once; the wait disarms it).
        void wait_slot_before_host_write();

        // One transfer batch: a command buffer from the transfer pool
        // and the fence its submission signals. A slot cycles
        // idle -> recording -> in_flight -> idle; the ring bytes and
        // dedicated staging buffers it holds are released when it is
        // retired.
        struct transfer_batch
        {
            enum class batch_state
            {
                idle,
                recording,
                in_flight
            };
            struct dedicated_staging
            {
                VkBuffer buffer{VK_NULL_HANDLE};
                VmaAllocation allocation{VK_NULL_HANDLE};
            };

            VkCommandBuffer cmd{VK_NULL_HANDLE};
            VkFence fence{VK_NULL_HANDLE};
            uint64_t id{0};
            batch_state state{batch_state::idle};
            // Something was recorded (or staged) into the open batch,
            // so flush submits it; an untouched batch goes back idle.
            bool recorded{false};
            // The submission reached the queue, so retiring the batch
            // has to wait for its fence. A batch whose submit failed is
            // retired in turn without a wait.
            bool submitted{false};
            std::vector<dedicated_staging> dedicated;
            // Destination buffers already copied into by this batch;
            // a second copy into one of them is preceded by a
            // transfer -> transfer barrier.
            std::vector<VkBuffer> written_buffers;
        };
        // The open batch, begun (or reused from an idle slot) on first
        // use. Null when none can be begun.
        transfer_batch* open_transfer_batch();
        // Release a completed batch: its ring bytes, its dedicated
        // staging buffers, and the slot.
        void retire_transfer_batch(transfer_batch& batch);
        // Reclaim submitted batches in submission order, polling their
        // fences: every batch up to the first one still executing.
        void retire_transfer_batches();
        // Block on the oldest submitted batch and retire it. Returns
        // false when there is none or the wait failed.
        bool wait_oldest_transfer_batch();
        // The submitted batch with the lowest id, or null.
        transfer_batch* oldest_transfer_batch();
        // True while a batch with an id up to @p batch_id is still
        // recording or executing — the gate a deferred destroy waits
        // behind.
        bool transfer_batch_live_up_to(uint64_t batch_id) const;
        // Drop every batch without a fence wait, releasing what it
        // holds: only under vkDeviceWaitIdle or once the device is
        // lost, when nothing executes any more (quit).
        void discard_transfer_batches();

        handle_pool<vk_buffer> m_buffers;
        handle_pool<vk_texture> m_textures;
        handle_pool<vk_sampler> m_samplers;
        handle_pool<vk_shader_module> m_shader_modules;
        handle_pool<vk_bind_group_layout> m_bind_group_layouts;
        handle_pool<vk_pipeline> m_pipelines;
        handle_pool<vk_bind_group> m_bind_groups;
        handle_pool<vk_render_target> m_render_targets;
        handle_pool<vk_query_set> m_query_sets;

        VkInstance m_instance{VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
        VkSurfaceKHR m_surface{VK_NULL_HANDLE};
        VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
        // The API version the instance was created with (what VMA is
        // told, capped by the physical device's own version).
        uint32_t m_api_version{VK_API_VERSION_1_1};
        VkDevice m_device{VK_NULL_HANDLE};
        VkQueue m_graphics_queue{VK_NULL_HANDLE};
        VkQueue m_present_queue{VK_NULL_HANDLE};
        uint32_t m_graphics_queue_family{0};
        uint32_t m_present_queue_family{0};
        // timestampValidBits of the graphics queue family: 0 means the
        // queue writes no usable timestamps.
        uint32_t m_timestamp_valid_bits{0};
        VmaAllocator m_allocator{VK_NULL_HANDLE};
        // See pipeline_cache(). m_pipeline_cache_file is empty when the
        // shader cache directory is disabled; m_pipeline_cache_digest is
        // the digest of the data read from it (0 when nothing was), so
        // quit skips rewriting an unchanged cache.
        VkPipelineCache m_pipeline_cache{VK_NULL_HANDLE};
        std::filesystem::path m_pipeline_cache_file;
        uint64_t m_pipeline_cache_digest{0};

        // Frame command buffers. Each slot is a command pool plus the
        // primary buffers allocated from it so far, handed out in order
        // by acquire_frame_command_buffer and reclaimed together by a
        // pool reset at begin_frame once the slot's fence wait has
        // proved them complete. m_frames_in_flight slots are in use;
        // m_frame_slot is the current frame's and advances at
        // end_frame.
        struct frame_command_slot
        {
            VkCommandPool pool{VK_NULL_HANDLE};
            std::vector<VkCommandBuffer> buffers;
            size_t next{0};

            // The secondary buffers of a parallel render pass, one pool
            // per recording lane (see acquire_secondary_command_buffer)
            // so the thread recording a lane's chunk never shares a pool
            // with another; handed out and reset exactly like the
            // primary buffers above. Lanes are appended on first use and
            // kept until quit.
            struct lane
            {
                VkCommandPool pool{VK_NULL_HANDLE};
                std::vector<VkCommandBuffer> buffers;
                size_t next{0};
            };
            std::vector<lane> lanes;
        };
        std::array<frame_command_slot, k_max_frames_in_flight> m_frame_command_slots{};

        // Serialise the two device paths the secondary encoders of a
        // parallel render pass reach from several threads at once (see
        // the file comment): the lazy VkPipeline variant build and cache
        // (graphics_pipeline_for) and the multi-buffered region sync a
        // bind performs (ensure_host_region_current). Uncontended on
        // the serial path.
        std::mutex m_pipeline_variant_mutex;
        std::mutex m_host_region_mutex;
        uint32_t m_frames_in_flight{1};
        uint32_t m_frame_slot{0};
        // Between begin_frame and end_frame. Decides which submission a
        // deferred destroy waits for and whether a host write to a
        // multi-buffered region must wait the slot's fence itself.
        bool m_in_frame{false};

        // Transfer batches (see the upload section above). The pool
        // allows per-buffer resets so an idle slot's buffer is reused
        // without touching the others; slots are appended as needed and
        // never freed before quit.
        VkCommandPool m_transfer_command_pool{VK_NULL_HANDLE};
        std::vector<transfer_batch> m_transfer_batches;
        // Index into m_transfer_batches of the recording batch, or
        // k_no_batch.
        static constexpr size_t k_no_batch = static_cast<size_t>(-1);
        size_t m_open_transfer_batch{k_no_batch};
        uint64_t m_next_transfer_batch_id{1};

        // The staging ring: one host-visible, host-coherent buffer,
        // mapped for the lifetime of the device, whose bytes are
        // handed out by m_staging_ring. Uploads larger than half of it
        // take a dedicated buffer instead (stage_upload).
        static constexpr VkDeviceSize k_staging_ring_bytes = 32ull * 1024ull * 1024ull;
        VkBuffer m_staging_buffer{VK_NULL_HANDLE};
        VmaAllocation m_staging_allocation{VK_NULL_HANDLE};
        uint8_t* m_staging_mapped{nullptr};
        staging_ring m_staging_ring;
        // Offset alignment of every ring reservation: the device's
        // optimalBufferCopyOffsetAlignment rounded up to a power of
        // two, at least 16 so any texel block of the engine's formats
        // (and the 4 bytes a depth copy needs) divides it.
        VkDeviceSize m_staging_alignment{16};

        // Grow-on-demand descriptor pool chain; allocations come from
        // the back, sets are freed to the pool recorded on their bind
        // group, and the whole chain is destroyed at quit. See
        // allocate_descriptor_set.
        std::vector<VkDescriptorPool> m_descriptor_pools;
        VkSampler m_fallback_sampler{VK_NULL_HANDLE};

        // Resolved depth formats, indexed depth24 / depth32_float /
        // depth24_stencil8; see vk_format_for.
        std::array<VkFormat, 3> m_depth_formats{VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
        // The physical device lists VK_KHR_portability_subset, which
        // the spec then requires the logical device to enable.
        bool m_has_portability_subset{false};

        // See device_lost(). m_device_lost_thrown records that
        // end_frame has already raised the loss to the main loop, so
        // it is thrown exactly once.
        bool m_device_lost{false};
        bool m_device_lost_thrown{false};

        VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
        VkSurfaceFormatKHR m_surface_format{};
        VkPresentModeKHR m_present_mode{VK_PRESENT_MODE_FIFO_KHR};
        VkExtent2D m_swapchain_extent{};
        std::vector<VkImage> m_swapchain_images;
        std::vector<VkImageView> m_swapchain_image_views;
        // One depth buffer per frame slot: the swapchain passes of two
        // frames in flight would otherwise write one image with no
        // dependency between them. A swapchain framebuffer pairs an
        // image with a slot's depth (see swapchain_framebuffer_index).
        std::array<VkImage, k_max_frames_in_flight> m_swapchain_depth_images{};
        std::array<VmaAllocation, k_max_frames_in_flight> m_swapchain_depth_allocations{};
        std::array<VkImageView, k_max_frames_in_flight> m_swapchain_depth_views{};
        // The engine-side format of the swapchain depth buffer; the
        // VkFormat backing it is vk_format_for(m_swapchain_depth_format)
        // once resolve_depth_formats has run.
        texture_format m_swapchain_depth_format{texture_format::depth32_float};
        render_target m_swapchain_target{};

        // Per frame slot: the semaphore its acquire signals and its
        // submission waits, and the fence its submission signals.
        // begin_frame waits the slot's fence before the frame records
        // anything and submit resets it right before the submission
        // that signals it; the fences are created signaled so the first
        // lap does not block. A fence is armed only by a submission
        // that succeeded, so a failed vkQueueSubmit (nothing will ever
        // signal the fence) does not leave a begin_frame waiting
        // forever; the serial it was armed with is what the wait
        // retires for the deferred destroys.
        std::array<VkSemaphore, k_max_frames_in_flight> m_image_available{};
        std::array<VkFence, k_max_frames_in_flight> m_in_flight_fences{};
        std::array<bool, k_max_frames_in_flight> m_in_flight_fence_armed{};
        std::array<uint64_t, k_max_frames_in_flight> m_fence_submit_serial{};
        // Queue submissions of frame command buffers so far, and the
        // highest serial a fence wait (or an idle wait) has proved
        // complete; see enqueue_destroy.
        uint64_t m_submit_serial{0};
        uint64_t m_completed_submit_serial{0};
        // One render-finished semaphore per swapchain image, indexed by the
        // acquired image index. A semaphore tied to a specific image is not
        // re-signaled until that image is re-acquired, which the acquire/fence
        // flow already gates, so the present operation never races a later
        // frame's submit (VUID-vkQueueSubmit-pSignalSemaphores-00067). Created
        // and destroyed alongside the swapchain so it tracks image-count
        // changes on resize.
        std::vector<VkSemaphore> m_render_finished;
        // The frame slot that last rendered into each swapchain image
        // (k_no_slot for none): the images-in-flight guard. An acquire
        // that hands an image back while the frame that last drew it is
        // still in flight waits that frame's fence first, so the image's
        // render-finished semaphore is never re-signaled while pending.
        static constexpr uint32_t k_no_slot = UINT32_MAX;
        std::vector<uint32_t> m_image_last_slot;
        uint32_t m_current_image_index{0};
        bool m_have_current_image{false};
        // Set by the first acquire_swapchain_image of a frame, whatever
        // its outcome, and cleared in end_frame: a frame gets exactly
        // one attempt, so a pass that opens after a failed acquire
        // does not acquire an image the earlier passes never drew to.
        bool m_acquire_attempted{false};
        // Set by submit once this frame's command buffer has been
        // queued against the acquired image; end_frame presents only
        // then, so a frame whose encoder failed to record does not
        // present an image whose render-finished semaphore will never
        // be signaled.
        bool m_present_pending{false};
        // True while there is nothing to present into: the surface
        // reported a 0x0 extent (minimised) or the last rebuild
        // failed. acquire_swapchain_image hands out no image, submit
        // takes the no-image path, end_frame has nothing to present,
        // and begin_frame polls the surface every frame until a rebuild
        // succeeds. Never set by init, which throws instead.
        bool m_swapchain_suspended{false};
        // See swapchain_generation().
        uint64_t m_swapchain_generation{0};
        // Source of vk_render_target::variant::render_pass_generation;
        // starts at 1 so 0 can mean "no render pass".
        uint64_t m_next_render_pass_generation{1};

        bool m_validation_enabled{false};
        // VK_EXT_debug_utils is enabled on the instance: labels and
        // object names reach validation messages and debuggers.
        bool m_debug_utils_enabled{false};
        PFN_vkCmdBeginDebugUtilsLabelEXT m_cmd_begin_debug_label{nullptr};
        PFN_vkCmdEndDebugUtilsLabelEXT m_cmd_end_debug_label{nullptr};
        PFN_vkSetDebugUtilsObjectNameEXT m_set_debug_object_name{nullptr};
        bool m_initialised{false};

        // Placeholder textures for unset sampler bindings; see
        // default_texture().
        texture m_default_texture_2d{};
        texture m_default_texture_cube{};

        // See enqueue_destroy: the closure, the frame submission it
        // waits for, and the newest transfer batch id at the time it
        // was queued (0 when none had begun).
        struct pending_destroy
        {
            uint64_t submit_serial{0};
            uint64_t transfer_batch_id{0};
            std::function<void()> fn;
        };
        std::vector<pending_destroy> m_pending_destroys;

        // Frame-level diagnostic counters. Logged at end_frame() for
        // @c k_diagnostic_frames frames after init so the user can
        // see whether scene_pass actually issued the cube draw.
        struct frame_stats
        {
            uint32_t passes_offscreen{0};
            uint32_t passes_swapchain{0};
            uint32_t draws{0};
            uint32_t draws_indexed{0};
            uint32_t vertices{0};
            uint32_t indices{0};
        };
        frame_stats m_frame_stats{};
        uint32_t m_frame_index{0};
        static constexpr uint32_t k_diagnostic_frames = 3;

        // VK_EXT_depth_clip_control lets Vulkan accept clip-space Z
        // in [-w, w] instead of the default [0, w] range. The
        // engine's projection matrices produce the [-w, w] range;
        // without this extension every pipeline would clip half the
        // view frustum and the framebuffer would stay at the clear
        // colour. We enable the extension when
        // available and chain VkPipelineViewportDepthClipControlCreateInfoEXT
        // into the viewport state of every graphics pipeline.
        bool m_depth_clip_control_enabled{false};

        // VK_EXT_extended_dynamic_state — needed for runtime stride
        // override on @c set_vertex_buffer. See the public accessor
        // for the rationale.
        bool m_extended_dynamic_state_enabled{false};
        PFN_vkCmdBindVertexBuffers2EXT m_cmd_bind_vertex_buffers2{nullptr};

        // Last drawable size the engine reported through
        // resize_swapchain, in pixels (seeded from window::pixel_size
        // at init, falling back to the logical settings size). Only
        // consulted when the surface leaves the extent to the
        // application (currentExtent == UINT32_MAX); everywhere else
        // the surface capabilities decide, so a rebuild never trusts
        // a stale cached size.
        uint32_t m_window_width{0};
        uint32_t m_window_height{0};
    };
} // namespace rendering_engine::gpu::backend::vulkan
