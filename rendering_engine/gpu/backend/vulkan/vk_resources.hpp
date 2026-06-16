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
 * @file vk_resources.hpp
 * @brief Vulkan-side record types stored inside the @c vk_device's
 *        handle_pool slots. Mirrors @c gl_resources.hpp.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    struct vk_buffer
    {
        VkBuffer object{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        size_t size{0};
        buffer_usage usage{0};
        buffer_usage_hint hint{buffer_usage_hint::static_data};

        // Persistent map for host-visible buffers (dynamic / stream
        // hint). Null for device-local buffers; those are written via
        // a staging buffer in @c vk_device::write_buffer.
        void* mapped{nullptr};
    };

    struct vk_texture
    {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};

        // Single-mip image views used when the texture is bound as a
        // storage image (the @c storage_texture binding kind), indexed
        // by mip level and built lazily in @c create_bind_group. A
        // storage descriptor must reference exactly one mip level, so
        // these are distinct from @c view (the whole-chain sampling
        // view). Released alongside the texture.
        std::vector<VkImageView> storage_views;

        // Built-in sampler set from the texture descriptor — mirrors
        // the GL backend's "sampler is part of the texture" model so
        // existing call sites continue to work without authoring a
        // separate sampler resource.
        VkSampler default_sampler{VK_NULL_HANDLE};

        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        texture_format format{texture_format::rgba8_unorm};
        VkFormat vk_format{VK_FORMAT_R8G8B8A8_UNORM};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t depth{1};
        uint32_t mip_levels{1};
        uint32_t array_layers{1};
        bool mipmaps{false};
        bool storage{false};
        bool is_depth{false};
        bool is_cube{false};
        bool is_3d{false};

        // True for swapchain image wrappers; the device does not own
        // the @c VkImage and must not destroy it.
        bool external{false};
    };

    struct vk_sampler
    {
        VkSampler object{VK_NULL_HANDLE};
        sampler_descriptor descriptor;
    };

    struct vk_shader_module
    {
        VkShaderModule object{VK_NULL_HANDLE};
        shader_stage stage{shader_stage::vertex};
    };

    struct vk_bind_group_layout
    {
        bind_group_layout_descriptor descriptor;
        VkDescriptorSetLayout object{VK_NULL_HANDLE};
    };

    struct vk_pipeline
    {
        VkPipelineLayout layout{VK_NULL_HANDLE};
        bool is_compute{false};

        // Compute pipelines are render-pass independent and built
        // up-front in @c create_compute_pipeline.
        VkPipeline compute_object{VK_NULL_HANDLE};

        // Graphics pipelines are bound to a specific render pass at
        // VkPipeline creation time. The engine renders into an
        // off-screen scene target *and* the swapchain through
        // different render passes, so a single pipeline_descriptor
        // has to materialise as one VkPipeline per render pass it
        // ends up drawing against. We lazy-build them in
        // @c vk_device::graphics_pipeline_for and cache them here.
        struct variant
        {
            VkRenderPass render_pass{VK_NULL_HANDLE};
            VkPipeline object{VK_NULL_HANDLE};
            // Off-screen targets render in Vulkan-natural Y-down so
            // that subsequent samplers see image row 0 == world-Z-down,
            // which is the convention the engine's tonemap shader
            // expects. Swapchain targets keep the OpenGL Y-up
            // convention via a negative-height viewport, so their
            // pipelines need the matching front-face flip. The two
            // variants are otherwise identical, so they're keyed by
            // the @c y_flipped flag in addition to render_pass.
            bool y_flipped{false};
        };
        std::vector<variant> graphics_variants;

        // Stored descriptor used to lazy-build the graphics
        // variants. Empty for compute pipelines.
        pipeline_descriptor descriptor;
    };

    struct vk_bind_group
    {
        bind_group_layout layout{};
        VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
        std::vector<binding_value> entries;
    };

    struct vk_render_target
    {
        // Render-pass variants keyed by (color_load, depth_load,
        // use_depth). The same target is used by passes that
        // disagree on load-op (the swapchain hosts tonemap with
        // LOAD_OP_CLEAR followed by ui with LOAD_OP_LOAD); each
        // unique tuple gets its own VkRenderPass and its own set
        // of framebuffers, and none are destroyed on a load-op
        // switch — that would dangle the pipeline cache, which
        // keys VkPipeline by the VkRenderPass it was built against.
        // Variants with different use_depth are not render-pass
        // compatible (different attachment counts), so the
        // framebuffers live on the variant rather than being
        // shared across variants.
        struct variant
        {
            VkAttachmentLoadOp color_load{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
            VkAttachmentLoadOp depth_load{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
            bool use_depth{false};
            VkRenderPass render_pass{VK_NULL_HANDLE};
            // Per-swapchain-image for swapchain targets; one entry
            // for off-screen targets.
            std::vector<VkFramebuffer> framebuffers;
        };
        std::vector<variant> variants;

        uint32_t width{0};
        uint32_t height{0};
        bool has_depth{true};

        // Color attachment exposed for next-pass sampling. Invalid
        // for swapchain targets.
        texture color_attachment{};
        texture depth_attachment{};

        bool is_swapchain{false};
    };
} // namespace rendering_engine::gpu::backend::vulkan
