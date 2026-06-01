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
 * Compute, indirect, storage and barrier methods are implemented as
 * focused stubs that the engine's existing pass set never exercises.
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

        std::unique_ptr<command_encoder> create_command_encoder() override;
        void submit(std::unique_ptr<command_encoder> encoder) override;

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
        void begin_frame();
        void end_frame();

        // One-shot command buffer for resource uploads.
        VkCommandBuffer begin_one_shot();
        void end_one_shot(VkCommandBuffer cmd);

        // Per-frame draw counters surfaced as a one-shot log for the
        // first few frames so a missing draw call is visible without
        // attaching RenderDoc. Cleared at submit time.
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
        // closure here; @c drain_pending_destroys is called after
        // @c vkWaitForFences in @c begin_frame and again under
        // vkDeviceWaitIdle on shutdown, so the actual vkDestroy*
        // call only fires once the GPU has finished referencing the
        // resource.
        void enqueue_destroy(std::function<void()> fn);
        void drain_pending_destroys();

    private:
        void create_instance();
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
        VkSemaphore m_render_finished{VK_NULL_HANDLE};
        VkFence m_in_flight_fence{VK_NULL_HANDLE};
        uint32_t m_current_image_index{0};
        bool m_have_current_image{false};

        bool m_validation_enabled{false};
        bool m_initialised{false};

        std::vector<std::function<void()>> m_pending_destroys;

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
