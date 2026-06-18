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
 * @file vk_device.hpp
 * @brief Vulkan implementation of @ref gpu::device.
 *
 * Mirrors @c gl_device.hpp: the @c vk_device class is declared in
 * full and member functions are split across translation units by
 * resource family (vk_device.cpp for lifecycle / swapchain /
 * encoders / lookup_*, vk_device_buffer.cpp for buffers, etc.).
 *
 * The backend records up to @c k_max_frames_in_flight frames ahead
 * of the GPU, runtime SPIR-V via @ref gpu::compile_glsl_to_spirv
 * (already used by the GL backend), no multi-threaded recording,
 * no real GPU allocator.
 * Compute pipelines and storage-image bind groups are implemented
 * (the IBL convolution runs on the GPU just like OpenGL); indirect
 * and barrier methods remain focused stubs that the engine's
 * existing pass set does not lean on.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/handle_pool.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    // Best-effort VkResult → human-readable string, used in error
    // logs so the user can pinpoint a failing call without validation
    // layers.
    const char* vk_result_to_string(VkResult r);

    struct vk_device : public device
    {
        vk_device();
        ~vk_device() override;

        void init() override;
        void quit() override;

        // The backend implements compute pipelines, storage-image
        // bind groups and the layout transitions the IBL convolution
        // needs, so the GPU prefilter path is taken just like OpenGL.
        bool supports_compute_prefilter() const override
        {
            return true;
        }

        buffer create_buffer(const buffer_descriptor& descriptor) override;
        texture create_texture(const texture_descriptor& descriptor) override;
        sampler create_sampler(const sampler_descriptor& descriptor) override;
        shader_module create_shader_module(const shader_module_descriptor& descriptor) override;
        bind_group_layout create_bind_group_layout(const bind_group_layout_descriptor& descriptor) override;
        pipeline create_pipeline(const pipeline_descriptor& descriptor) override;
        pipeline create_compute_pipeline(const compute_pipeline_descriptor& descriptor) override;
        bind_group create_bind_group(const bind_group_descriptor& descriptor) override;

        void destroy(buffer handle) override;
        void destroy(texture handle) override;
        void destroy(sampler handle) override;
        void destroy(shader_module handle) override;
        void destroy(bind_group_layout handle) override;
        void destroy(pipeline handle) override;
        void destroy(bind_group handle) override;

        void write_buffer(buffer buffer_handle, const void* data, size_t size, size_t offset) override;
        void write_texture(texture texture_handle, const void* data, size_t size) override;
        void write_texture_3d(texture texture_handle, const void* data, size_t size) override;
        void write_cube_face(texture texture_handle, cube_face face, const void* data, size_t size) override;
        void generate_mipmaps(texture texture_handle) override;

        render_target swapchain_target() override;
        void resize_swapchain(uint32_t width, uint32_t height) override;
        render_target create_render_target(const render_target_descriptor& descriptor) override;
        void destroy(render_target handle) override;
        texture render_target_color_texture(render_target handle) override;
        texture render_target_depth_texture(render_target handle) override;

        std::unique_ptr<command_encoder> create_command_encoder(uint32_t recording_context = 0) override;
        void submit(std::unique_ptr<command_encoder> encoder) override;

        // Vulkan records passes concurrently into per-context command pools and
        // submits the resulting buffers as one ordered batch.
        bool supports_parallel_recording() const override
        {
            return true;
        }
        uint32_t recording_context_count() const override
        {
            return k_max_recording_contexts;
        }
        void submit_frame(std::vector<std::unique_ptr<command_encoder>> encoders) override;

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

        // Vulkan handles + helpers exposed to per-resource TUs.
        VkInstance instance() const noexcept;
        VkDevice vk_handle() const noexcept;
        VkPhysicalDevice physical_device() const noexcept;
        VkQueue graphics_queue() const noexcept;
        uint32_t graphics_queue_family() const noexcept;
        VkCommandPool command_pool() const noexcept;
        VkDescriptorPool descriptor_pool() const noexcept;
        // Number of images the swapchain was created with — surfaced so
        // the Dear ImGui Vulkan backend can size its frame resources.
        uint32_t swapchain_image_count() const noexcept;
        uint32_t current_swapchain_image_index() const noexcept;
        bool have_current_swapchain_image() const noexcept;
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

        // Acquire (or rebuild) the render pass + framebuffers
        // compatible with @p target for the requested load ops.
        VkRenderPass acquire_render_pass(vk_render_target& target,
                                         VkAttachmentLoadOp color_load,
                                         VkAttachmentLoadOp depth_load,
                                         bool use_depth);

        // The render pass + framebuffer a render-pass encoder begins with.
        struct render_pass_begin_info
        {
            VkRenderPass render_pass{VK_NULL_HANDLE};
            VkFramebuffer framebuffer{VK_NULL_HANDLE};
        };

        // Resolve the render pass and framebuffer to begin for @p target with
        // the given load ops, under the cache lock. @p swapchain_image selects
        // the framebuffer for the swapchain target (ignored off-screen, which
        // has a single framebuffer). Folds the lazy render-pass/framebuffer
        // build and the framebuffer pick into one critical section so passes
        // recorded on worker threads cannot race the shared variant list.
        render_pass_begin_info begin_info_for(vk_render_target& target,
                                              VkAttachmentLoadOp color_load,
                                              VkAttachmentLoadOp depth_load,
                                              bool use_depth,
                                              uint32_t swapchain_image);

        // Look up — or lazily build — the @c VkPipeline for
        // @p handle that is compatible with @p render_pass. The
        // engine creates pipelines once but binds them across
        // render passes with different attachment formats (the
        // off-screen scene target vs. the swapchain), so each
        // pipeline_descriptor materialises into one VkPipeline per
        // render pass it draws against. The cache is owned by the
        // pipeline record and torn down with it.
        // @p y_flipped selects the front-face mapping: swapchain
        // passes render through a negative-height viewport (CCW
        // → CW), off-screen passes don't (CCW stays CCW).
        VkPipeline graphics_pipeline_for(pipeline handle, VkRenderPass render_pass, bool y_flipped);

        // Acquire the next swapchain image. Idempotent within a
        // frame.
        void begin_frame() override;
        void end_frame();

        // One-shot command buffer for resource uploads. begin_one_shot reuses
        // a single dedicated upload command buffer (reset on each call), and
        // end_one_shot submits it against a dedicated fence and waits on that
        // fence rather than draining the whole graphics queue with
        // vkQueueWaitIdle. Uploads are synchronous: the data is resident when
        // end_one_shot returns. Not re-entrant — one upload at a time.
        VkCommandBuffer begin_one_shot();
        void end_one_shot(VkCommandBuffer cmd);

        // Copy @p size bytes from @p data into the shared, growable host-visible
        // staging buffer and return the buffer to copy out of (source offset 0).
        // Reused across every upload instead of allocating and freeing a staging
        // buffer per call; safe because uploads are synchronous (end_one_shot
        // waits before the next upload reuses the buffer). Returns VK_NULL_HANDLE
        // on allocation failure or zero size.
        VkBuffer stage_upload(const void* data, VkDeviceSize size);

        // Per-recording-context, per-frame-in-flight command-buffer pool. An
        // encoder borrows a primary command buffer from its @p recording_context
        // with acquire_command_buffer (reusing a reset buffer from that
        // context's current-slot free list, or allocating one on a miss).
        // submit parks the submitted buffer until the slot's fence signals,
        // then begin_frame recycles it back to the free list — so steady-state
        // rendering reuses a handful of buffers per context instead of
        // allocating and freeing them every frame. discard_command_buffer
        // returns an un-submitted buffer (an error/early-out path) straight to
        // the free list. Each recording context has its own VkCommandPool, so
        // distinct contexts can be recorded on different threads concurrently
        // (Vulkan command pools are not internally synchronised); a given
        // context is only ever touched by one thread at a time.
        VkCommandBuffer acquire_command_buffer(uint32_t recording_context);
        void discard_command_buffer(uint32_t recording_context, VkCommandBuffer cmd);

        // Lazily create (and cache on the texture) the single-mip image
        // view used to bind @p tex as a storage image at @p level. Cube
        // and 3D textures bind every layer through one view; the level
        // selects the subresource. Returns VK_NULL_HANDLE if the level
        // is out of range or the view cannot be created.
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
        // attaching RenderDoc. Cleared at submit time.
        void note_render_pass_opened(bool is_swapchain, bool use_depth);
        void note_draw(uint32_t vertex_count);
        void note_draw_indexed(uint32_t index_count);

        // Find a memory type matching @p type_filter and the
        // requested @p properties.
        uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

        // Destroy callbacks queued from @c destroy() overloads. Freeing a
        // buffer or descriptor set during the frame that submitted it would
        // land the free while the GPU is still reading from it; the engine's
        // per-draw UBO / bind-group churn used to trigger streams of
        // VUID-vkDestroyBuffer-buffer-00922 / VUID-vkFreeDescriptor
        // Sets-pDescriptorSets-00309. Each @c destroy() pushes a closure into
        // the bucket of the frame-in-flight currently being recorded; that
        // bucket is drained in @c begin_frame after its slot's fence signals
        // (so the work that referenced the resource is done) and all buckets
        // are drained under vkDeviceWaitIdle on shutdown. The bucket must be
        // per-slot, not global: with N frames in flight, waiting on one slot's
        // fence says nothing about whether the other slots' GPU work is done.
        void enqueue_destroy(std::function<void()> fn);
        void drain_pending_destroys(uint32_t frame_in_flight);

        // 1x1 placeholder textures (one 2D, one cube) bound in place of
        // an unset sampler slot. Vulkan requires every statically-used
        // descriptor to reference a valid resource, so create_bind_group
        // substitutes the matching-dimension default when a material
        // leaves a texture binding empty (the OpenGL backend just leaves
        // the sampler unbound). Created in init(), released in quit().
        texture default_texture(texture_dimension dim) const noexcept;

    private:
        void create_instance();
        void create_default_textures();
        void create_debug_messenger();
        void destroy_debug_messenger();
        void create_surface();
        void pick_physical_device();
        void create_logical_device();
        void create_command_pool();
        void create_descriptor_pool();
        void create_swapchain();
        void destroy_swapchain();
        void create_sync_objects();
        void destroy_sync_objects();

        // Reset every command buffer submitted under @p frame_in_flight's fence
        // across all recording contexts and move it back to that context's free
        // list. Called from begin_frame after the slot's fence has signalled,
        // so the GPU is finished with them.
        void recycle_in_flight_command_buffers(uint32_t frame_in_flight);

        handle_pool<vk_buffer> m_buffers;
        handle_pool<vk_texture> m_textures;
        handle_pool<vk_sampler> m_samplers;
        handle_pool<vk_shader_module> m_shader_modules;
        handle_pool<vk_bind_group_layout> m_bind_group_layouts;
        handle_pool<vk_pipeline> m_pipelines;
        handle_pool<vk_bind_group> m_bind_groups;
        handle_pool<vk_render_target> m_render_targets;

        VkInstance m_instance{VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT m_debug_messenger{VK_NULL_HANDLE};
        VkSurfaceKHR m_surface{VK_NULL_HANDLE};
        VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
        VkPhysicalDeviceMemoryProperties m_memory_properties{};
        VkDevice m_device{VK_NULL_HANDLE};
        VkQueue m_graphics_queue{VK_NULL_HANDLE};
        VkQueue m_present_queue{VK_NULL_HANDLE};
        uint32_t m_graphics_queue_family{0};
        uint32_t m_present_queue_family{0};
        VkCommandPool m_command_pool{VK_NULL_HANDLE};
        VkDescriptorPool m_descriptor_pool{VK_NULL_HANDLE};

        VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
        VkSurfaceFormatKHR m_surface_format{};
        VkPresentModeKHR m_present_mode{VK_PRESENT_MODE_FIFO_KHR};
        VkExtent2D m_swapchain_extent{};
        std::vector<VkImage> m_swapchain_images;
        std::vector<VkImageView> m_swapchain_image_views;
        VkImage m_swapchain_depth_image{VK_NULL_HANDLE};
        VkDeviceMemory m_swapchain_depth_memory{VK_NULL_HANDLE};
        VkImageView m_swapchain_depth_view{VK_NULL_HANDLE};
        texture_format m_swapchain_depth_format{texture_format::depth32_float};
        render_target m_swapchain_target{};

        // Number of frames the CPU may record ahead of the GPU. With a
        // single frame in flight the CPU stalled on the in-flight fence
        // every frame; a small ring of per-frame sync resources lets the
        // CPU record frame N+1 while the GPU is still draining frame N.
        // Two is the usual sweet spot — enough to hide the submit/acquire
        // latency without adding more than one frame of input lag.
        static constexpr uint32_t k_max_frames_in_flight = 2;

        // Per-frame-in-flight sync resources, indexed by m_frame_in_flight.
        // The image-available semaphore must be per-frame: vkAcquireNextImageKHR
        // signals it, and the previous frame's acquire may still be pending
        // when we record the next, so reusing one semaphore would race
        // (VUID-vkAcquireNextImageKHR-semaphore-01779). The fence gates reuse
        // of the slot's resources.
        std::array<VkSemaphore, k_max_frames_in_flight> m_image_available{};
        std::array<VkFence, k_max_frames_in_flight> m_in_flight_fences{};
        uint32_t m_frame_in_flight{0};

        // One render-finished semaphore per swapchain image, indexed by the
        // acquired image index. A semaphore tied to a specific image is not
        // re-signaled until that image is re-acquired, which the acquire/fence
        // flow already gates, so the present operation never races a later
        // frame's submit (VUID-vkQueueSubmit-pSignalSemaphores-00067). Created
        // and destroyed alongside the swapchain so it tracks image-count
        // changes on resize.
        std::vector<VkSemaphore> m_render_finished;

        // Tracks which frame-in-flight fence last rendered to each swapchain
        // image (no ownership — the fences live in m_in_flight_fences). When
        // there are fewer images than frames in flight, acquire can hand back
        // an image a previous frame is still drawing to; begin_frame waits on
        // this fence before reusing the image. Sized to the image count and
        // reset on resize.
        std::vector<VkFence> m_images_in_flight;
        uint32_t m_current_image_index{0};
        bool m_have_current_image{false};

        bool m_validation_enabled{false};
        bool m_initialised{false};

        // Placeholder textures for unset sampler bindings; see
        // default_texture().
        texture m_default_texture_2d{};
        texture m_default_texture_cube{};

        std::array<std::vector<std::function<void()>>, k_max_frames_in_flight> m_pending_destroys;

        // Upper bound on concurrently-recorded encoders. Each gets its own
        // command pool because Vulkan command pools cannot be recorded from
        // more than one thread at once; the engine caps the groups it fans out
        // to by this, by the worker count, and by the pass count.
        static constexpr uint32_t k_max_recording_contexts = 8;

        // One command pool per recording context, each with its own
        // per-frame-in-flight free / in-flight command-buffer lists. A context
        // is only ever touched by one thread at a time (the engine maps each
        // parallel group to a distinct context), so no locking is needed on the
        // lists themselves; the pool is created lazily on first use. Recycled by
        // recycle_in_flight_command_buffers once the slot's fence signals; the
        // pools (and their buffers) are destroyed in quit().
        struct recording_context
        {
            VkCommandPool pool{VK_NULL_HANDLE};
            std::array<std::vector<VkCommandBuffer>, k_max_frames_in_flight> free;
            std::array<std::vector<VkCommandBuffer>, k_max_frames_in_flight> in_flight;
        };
        std::array<recording_context, k_max_recording_contexts> m_recording_contexts;

        // Guards the lazily-built render-pass and graphics-pipeline caches
        // (acquire_render_pass / begin_info_for / graphics_pipeline_for) so the
        // jobs-driven parallel pass recording can build/look up cache entries
        // from several threads at once. Uncontended on the serial path.
        std::mutex m_cache_mutex;
        // Guards the per-frame draw/pass counters, which every recorded draw
        // bumps; separate from m_cache_mutex so a draw counter update does not
        // serialise against a cache lookup.
        std::mutex m_stats_mutex;

        // Reusable upload command buffer + fence backing begin_one_shot /
        // end_one_shot. The command buffer is allocated lazily and reset on
        // reuse; the fence lets an upload wait on its own submission instead of
        // idling the whole queue. The command buffer is freed implicitly with
        // the command pool; the fence is owned by create/destroy_sync_objects.
        VkCommandBuffer m_upload_cmd{VK_NULL_HANDLE};
        VkFence m_upload_fence{VK_NULL_HANDLE};

        // Shared, growable, persistently-mapped host-visible staging buffer used
        // by every CPU->GPU upload (see stage_upload). Grown on demand to the
        // largest request; freed in quit().
        VkBuffer m_staging_buffer{VK_NULL_HANDLE};
        VkDeviceMemory m_staging_memory{VK_NULL_HANDLE};
        void* m_staging_mapped{nullptr};
        VkDeviceSize m_staging_capacity{0};

        // Frame-level diagnostic counters. Logged at submit() for
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
        // in [-w, w] (OpenGL convention) instead of the default
        // [0, w] range. The engine's projection matrices are
        // GL-style; without this extension every pipeline would
        // clip half the view frustum and the framebuffer would
        // stay at the clear colour. We enable the extension when
        // available and chain VkPipelineViewportDepthClipControlCreateInfoEXT
        // into the viewport state of every graphics pipeline.
        bool m_depth_clip_control_enabled{false};

        // VK_EXT_extended_dynamic_state — needed for runtime stride
        // override on @c set_vertex_buffer. See the public accessor
        // for the rationale.
        bool m_extended_dynamic_state_enabled{false};
        PFN_vkCmdBindVertexBuffers2EXT m_cmd_bind_vertex_buffers2{nullptr};

        uint32_t m_window_width{0};
        uint32_t m_window_height{0};
    };
} // namespace rendering_engine::gpu::backend::vulkan
