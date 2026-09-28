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
 * (rendering_engine::graphics_settings::frames_in_flight, 2 by default): each
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
 * with its own fence ahead of the frame (see vk_transfer.hpp). Compute
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
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_descriptor_allocator.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_frame.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_instance.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_physical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_pipeline_cache.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_query_pool.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_render_pass_cache.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_swapchain.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_transfer.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
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

        void init(const surface_desc& surface, uint32_t frames_in_flight) override;
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

        // Dear ImGui's Vulkan renderer backend behind the overlay
        // interface, drawing into the debug pass's swapchain render
        // pass (vk_overlay_renderer.cpp); null in a build without ImGui.
        std::unique_ptr<overlay_renderer> create_overlay_renderer() override;

        // The slot ring: see gpu::device. The count init was given,
        // clamped to k_max_frames_in_flight.
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
        // looping on a dead device. See
        // vk_logical_device::mark_device_lost.
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
        // Counter bumped on every successful swapchain (re)build: 1 after
        // the first build, one more after each rebuild. The swapchain
        // log line reports it.
        uint64_t swapchain_generation() const noexcept;
        uint32_t current_swapchain_image_index() const noexcept;
        bool have_current_swapchain_image() const noexcept;
        // Index into a swapchain variant's framebuffers of the one
        // that attaches swapchain image @p image_index together with
        // the current frame slot's depth image (one per slot, so two
        // frames in flight never share a depth buffer).
        uint32_t swapchain_framebuffer_index(uint32_t image_index) const noexcept;
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

        // The render pass + framebuffers of @p target for @p key, or
        // for a single load op per attachment kind (see
        // vk_render_pass_cache).
        VkRenderPass acquire_render_pass(vk_render_target& target, const vk_render_pass_key& key);
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
        // (see vk_render_pass_cache::retire_render_pass_variants),
        // which is why the key
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

        // Record a whole-image layout transition of @p tex into
        // @p cmd and update the tracked layout. No-op when the image
        // is already in @p new_layout. Used by the encoder's copies,
        // which need the transfer layouts around a copy and the
        // resting layout back afterwards.
        void record_layout_transition(VkCommandBuffer cmd, vk_texture& tex, VkImageLayout new_layout);

        // Acquire the next swapchain image for the current frame, once
        // per frame, when the first swapchain-targeted pass opens (see
        // vk_swapchain::acquire_swapchain_image). Because an
        // out-of-date acquire rebuilds the swapchain and so retires the
        // swapchain target's render-pass variants, callers read nothing
        // from that target until this returns.
        void acquire_swapchain_image();

        // A primary command buffer for a command encoder, from the
        // current frame's pool (see vk_frame).
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

        // The frame's draw counters (see vk_frame::note_draw).
        void note_render_pass_opened(bool is_swapchain, bool use_depth);
        void note_draw(uint32_t vertex_count);
        void note_draw_indexed(uint32_t index_count);
        void note_draws(uint32_t draws, uint32_t vertices, uint32_t draws_indexed, uint32_t indices);

        // A secondary command buffer for a parallel render pass's chunk,
        // from recording lane @p lane (see vk_frame).
        VkCommandBuffer acquire_secondary_command_buffer(uint32_t lane);

        // Queue @p fn, which destroys a Vulkan object, to run once no
        // queue submission or transfer batch can still reference it
        // (see vk_frame::enqueue_destroy).
        void enqueue_destroy(std::function<void()> fn);

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
        void create_default_textures();
        // Fill the base class's device_features / device_limits from
        // the physical device's properties and the grants recorded by
        // create_logical_device.
        void query_capabilities();
        // A compute VkPipeline over @p module and @p layout, through the
        // pipeline cache; VK_NULL_HANDLE (logged) on failure.
        VkPipeline build_compute_pipeline(VkShaderModule module, VkPipelineLayout layout);
        // The sampler substituted for a texture whose own sampler
        // failed to create, so a null sampler never reaches a
        // combined-image-sampler descriptor.
        void create_fallback_sampler();
        // Forced rebuild used by every recovery site: re-queries the
        // surface capabilities (never the cached window size — an
        // OS-driven out-of-date arrives without a resize), waits the
        // device idle, retires the swapchain target's render-pass
        // variants and builds a new swapchain at the surface's extent.
        // A 0x0 extent (minimised) or a failed build leaves the device
        // suspended instead of throwing; a later call resumes it.
        // Returns true when a new swapchain is live.
        bool recreate_swapchain();
        // Bring @p slot's region of @p record up to date except for
        // [@p skip_begin, @p skip_end), which the caller is about to
        // overwrite: copies the rest of the region's gap from the latest
        // region and clears the gap.
        void sync_host_region(vk_buffer& record, uint32_t slot, VkDeviceSize skip_begin, VkDeviceSize skip_end);

        handle_pool<vk_buffer> m_buffers;
        handle_pool<vk_texture> m_textures;
        handle_pool<vk_sampler> m_samplers;
        handle_pool<vk_shader_module> m_shader_modules;
        handle_pool<vk_bind_group_layout> m_bind_group_layouts;
        handle_pool<vk_pipeline> m_pipelines;
        handle_pool<vk_bind_group> m_bind_groups;
        handle_pool<vk_render_target> m_render_targets;

        // The components, in bring-up order; quit shuts them down in
        // reverse (see quit), and their destructors release nothing.
        vk_instance m_instance;
        vk_physical_device m_physical_device;
        vk_logical_device m_device{m_instance, m_physical_device};
        vk_pipeline_cache m_pipeline_cache;
        vk_transfer m_transfer{m_physical_device, m_device};
        vk_frame m_frame{m_physical_device, m_device, m_transfer};
        vk_descriptor_allocator m_descriptors{m_device};
        vk_query_pool m_queries{m_device, m_frame};
        vk_swapchain m_swapchain{
            m_instance, m_physical_device, m_device, m_frame, [this] { return recreate_swapchain(); }};
        vk_render_pass_cache m_render_passes{
            m_physical_device, m_device, m_frame, m_swapchain, m_textures, m_pipelines};

        // Serialise the two device paths the secondary encoders of a
        // parallel render pass reach from several threads at once (see
        // the file comment): the lazy VkPipeline variant build and cache
        // (graphics_pipeline_for) and the multi-buffered region sync a
        // bind performs (ensure_host_region_current). Uncontended on
        // the serial path.
        std::mutex m_pipeline_variant_mutex;
        std::mutex m_host_region_mutex;

        VkSampler m_fallback_sampler{VK_NULL_HANDLE};

        // end_frame has already raised the device loss to the main
        // loop, so it is thrown exactly once.
        bool m_device_lost_thrown{false};

        render_target m_swapchain_target{};

        bool m_initialised{false};

        // Placeholder textures for unset sampler bindings; see
        // default_texture().
        texture m_default_texture_2d{};
        texture m_default_texture_cube{};
    };
} // namespace rendering_engine::gpu::backend::vulkan
