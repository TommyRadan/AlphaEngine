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
 * The backend ships with a single frame in flight, runtime SPIR-V
 * via @ref gpu::compile_glsl_to_spirv (already used by the GL
 * backend), no multi-threaded recording, no real GPU allocator.
 * Compute pipelines and storage-image bind groups are implemented
 * (the IBL convolution runs on the GPU just like OpenGL); indirect
 * and barrier methods remain focused stubs that the engine's
 * existing pass set does not lean on.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
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
        // Window-size hint. A size that matches the live swapchain is
        // a no-op; 0x0 suspends presentation (minimised); anything
        // else rebuilds the swapchain against the surface's *current*
        // capabilities — the surface, not the hint, decides the
        // extent (see recreate_swapchain).
        void resize_swapchain(uint32_t width, uint32_t height) override;
        bool swapchain_suspended() const noexcept override;
        render_target create_render_target(const render_target_descriptor& descriptor) override;
        void destroy(render_target handle) override;
        texture render_target_color_texture(render_target handle) override;
        texture render_target_depth_texture(render_target handle) override;

        std::unique_ptr<command_encoder> create_command_encoder() override;
        // Queue the encoder's command buffer. Inside a frame that has
        // acquired a swapchain image the submission waits the
        // image-available semaphore, signals that image's
        // render-finished semaphore and the in-flight fence, and
        // leaves the present to end_frame. Outside a frame (or in a
        // frame whose passes never reached the swapchain) the work is
        // submitted and waited for immediately.
        void submit(std::unique_ptr<command_encoder> encoder) override;

        // Frame boundary. begin_frame waits the in-flight fence for
        // the previous frame's command buffer and drains the
        // deferred-destroy queue, so every host write the renderer
        // makes afterwards lands in memory the GPU is done with. It
        // does not acquire an image; while the swapchain is suspended
        // it polls the surface and rebuilds as soon as the extent is
        // usable again. end_frame presents the image acquired this
        // frame (when submit queued work against it), rebuilds the
        // swapchain if the present reported it out of date or
        // suboptimal, and rolls the per-frame bookkeeping.
        void begin_frame() override;
        void end_frame() override;

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
        // Bumped on every successful swapchain (re)build. Anything that
        // baked a swapchain render pass into its own objects — the
        // Dear ImGui Vulkan backend builds its pipeline against one —
        // compares this before recording and rebuilds when it moved;
        // the render passes it knew are retired by then.
        uint64_t swapchain_generation() const noexcept;
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
        VkPipeline graphics_pipeline_for(pipeline handle,
                                         VkRenderPass render_pass,
                                         uint64_t render_pass_generation,
                                         bool y_flipped);

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
        // until this returns. The in-flight fence is not touched here —
        // submit resets it right before the queue submission that
        // signals it, so a failed acquire never leaves it unsignaled
        // for the next begin_frame to block on.
        void acquire_swapchain_image();

        // One-shot command buffer for resource uploads.
        VkCommandBuffer begin_one_shot();
        void end_one_shot(VkCommandBuffer cmd);

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
        // attaching RenderDoc. Cleared in end_frame.
        void note_render_pass_opened(bool is_swapchain, bool use_depth);
        void note_draw(uint32_t vertex_count);
        void note_draw_indexed(uint32_t index_count);

        // Find a memory type matching @p type_filter and the
        // requested @p properties.
        uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

        // Destroy callbacks queued from @c destroy() overloads. With
        // a single frame in flight, freeing a buffer or descriptor
        // set during the frame that submitted it would land the
        // free while the GPU is still reading from it; the engine's
        // per-draw UBO / bind-group churn used to trigger streams of
        // VUID-vkDestroyBuffer-buffer-00922 / VUID-vkFreeDescriptor
        // Sets-pDescriptorSets-00309. Each @c destroy() pushes a
        // closure here; @c drain_pending_destroys runs only at two
        // points where nothing can reference the resources: in
        // @c begin_frame, after @c vkWaitForFences and before the
        // renderer records anything for the new frame (so a bind
        // group a material rebuilds mid-frame is never freed while
        // the open command buffer already references it), and under
        // vkDeviceWaitIdle in @c quit. Destroys enqueued outside a
        // frame — the IBL prefilter scaffold, start-up uploads —
        // simply wait for the next of those two points.
        void enqueue_destroy(std::function<void()> fn);
        void drain_pending_destroys();

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
        // Build the swapchain for @p extent plus everything hanging off
        // it (image views, the shared depth buffer, the per-image
        // render-finished semaphores). The previous swapchain, if any,
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

        VkSemaphore m_image_available{VK_NULL_HANDLE};
        // One render-finished semaphore per swapchain image, indexed by the
        // acquired image index. A semaphore tied to a specific image is not
        // re-signaled until that image is re-acquired, which the acquire/fence
        // flow already gates, so the present operation never races a later
        // frame's submit (VUID-vkQueueSubmit-pSignalSemaphores-00067). Created
        // and destroyed alongside the swapchain so it tracks image-count
        // changes on resize.
        std::vector<VkSemaphore> m_render_finished;
        // Signaled by the frame submission in submit(); waited in
        // begin_frame before the next frame records anything and reset
        // right before the submission that signals it. Created
        // signaled so frame 0 does not block.
        VkFence m_in_flight_fence{VK_NULL_HANDLE};
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
        bool m_initialised{false};

        // Placeholder textures for unset sampler bindings; see
        // default_texture().
        texture m_default_texture_2d{};
        texture m_default_texture_cube{};

        std::vector<std::function<void()>> m_pending_destroys;

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
