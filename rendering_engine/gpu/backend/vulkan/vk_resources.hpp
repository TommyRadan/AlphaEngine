// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_resources.hpp
 * @brief Vulkan-side record types stored inside the @c vk_device's
 *        handle_pool slots. Mirrors @c gl_resources.hpp.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/vulkan/vk_allocator.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    // Most frames the backend can keep in flight; the per-frame rings
    // (sync objects, command pools, swapchain depth images, the copies
    // of a dynamic buffer, a bind group's descriptor sets) are sized by
    // it and the runtime count (core::graphics_settings::frames_in_flight)
    // is clamped to it.
    inline constexpr uint32_t k_max_frames_in_flight = 2;

    struct vk_buffer
    {
        VkBuffer object{VK_NULL_HANDLE};
        // The VMA allocation backing @c object; the buffer and the
        // allocation are created and destroyed as one
        // (vmaCreateBuffer / vmaDestroyBuffer).
        VmaAllocation allocation{VK_NULL_HANDLE};
        // The size the caller asked for: what one copy holds and what
        // every offset is checked against. @c object is
        // @c region_count * @c region_stride bytes long.
        size_t size{0};
        buffer_usage usage{0};
        buffer_usage_hint hint{buffer_usage_hint::static_data};

        // Persistent map for host-visible buffers (dynamic / stream
        // hint), owned by the allocation and released with it. Null for
        // device-local buffers; those are written through the staging
        // ring in @c vk_device::write_buffer.
        void* mapped{nullptr};

        // A dynamic_data buffer on a device with several frames in
        // flight holds one copy ("region") of its @c size bytes per
        // frame slot, @c region_stride apart (the size rounded up to
        // the uniform / storage offset alignment), so the host writes
        // the current frame's copy while the frames in flight read
        // theirs. Every write lands in the current slot's region; the
        // regions of the other slots fall behind and each records, as
        // one byte span, the union of the writes it has missed
        // (@c gap): before a region is written or bound again the
        // device copies that span across from @c latest_region — the
        // region every write so far has reached — so a buffer written
        // once and one rewritten every frame both read as a single
        // buffer would. A single-region buffer (static, stream, or one
        // frame in flight) has @c region_count 1 and no bookkeeping.
        struct region_gap
        {
            VkDeviceSize begin{0};
            VkDeviceSize end{0};

            bool empty() const noexcept
            {
                return end <= begin;
            }
        };
        uint32_t region_count{1};
        VkDeviceSize region_stride{0};
        uint32_t latest_region{0};
        std::array<region_gap, k_max_frames_in_flight> gaps{};
    };

    struct vk_texture
    {
        VkImage image{VK_NULL_HANDLE};
        // Null for swapchain wrappers (@c external), which the device
        // does not allocate.
        VmaAllocation allocation{VK_NULL_HANDLE};
        // The whole-chain sampling view: every level and layer, the
        // depth aspect only for a depth-stencil format (a sampled view
        // names one aspect).
        VkImageView view{VK_NULL_HANDLE};

        // Single-mip image views used when the texture is bound as a
        // storage image (the @c storage_texture binding kind), indexed
        // by mip level and built lazily in @c create_bind_group. A
        // storage descriptor must reference exactly one mip level, so
        // these are distinct from @c view (the whole-chain sampling
        // view). Released alongside the texture.
        std::vector<VkImageView> storage_views;

        // Single-level, single-layer 2D views a framebuffer attaches
        // the texture through, indexed @c layer * mip_levels + mip and
        // built lazily by @c vk_device::attachment_image_view. Every
        // aspect of the format, since an attachment writes them all.
        // Released alongside the texture.
        std::vector<VkImageView> attachment_views;

        // Built-in sampler set from the texture descriptor — mirrors
        // the GL backend's "sampler is part of the texture" model so
        // existing call sites continue to work without authoring a
        // separate sampler resource.
        VkSampler default_sampler{VK_NULL_HANDLE};

        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        texture_format format{texture_format::rgba8_unorm};
        // The VkFormat actually backing the image. For depth formats
        // this is what the device's fallback chain resolved at init
        // (see vk_device::vk_format_for), not a fixed translation of
        // @c format, so the aspect is recorded alongside it rather
        // than re-derived from the engine format.
        VkFormat vk_format{VK_FORMAT_R8G8B8A8_UNORM};
        VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t depth{1};
        uint32_t mip_levels{1};
        // Six for a cube, the descriptor's count for an array, 1
        // otherwise.
        uint32_t array_layers{1};
        // Samples per texel; above 1 the image is a multisampled
        // attachment with a single level and no uploads.
        uint32_t samples{1};
        texture_usage usage{texture_usage_default};
        bool mipmaps{false};
        // The format supports the linear blit generate_mipmaps derives
        // the chain with.
        bool blit_capable{false};
        bool storage{false};
        bool is_depth{false};
        bool is_cube{false};
        bool is_array{false};
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
        // up-front in @c create_compute_pipeline, from
        // @c compute_shader (kept so the debug hot reload can rebuild
        // the pipeline when that module's code is replaced).
        VkPipeline compute_object{VK_NULL_HANDLE};
        shader_module compute_shader{};

        // Graphics pipelines are bound to a specific render pass at
        // VkPipeline creation time. The engine renders into an
        // off-screen scene target *and* the swapchain through
        // different render passes, so a single pipeline_descriptor
        // has to materialise as one VkPipeline per render pass it
        // ends up drawing against. We lazy-build them in
        // @c vk_device::graphics_pipeline_for and cache them here.
        //
        // A variant is keyed by the render pass's generation (see
        // @c vk_render_target::variant) as well as its handle: the
        // swapchain's render passes are retired on every rebuild
        // and a driver is free to hand a new pass the same handle
        // value, so a handle-only match could return a pipeline
        // built against a freed pass. When a render pass is retired
        // the device purges every variant carrying its generation
        // (@c vk_device::retire_render_pass_variants), so nothing
        // here ever outlives the pass it was built for.
        struct variant
        {
            VkRenderPass render_pass{VK_NULL_HANDLE};
            uint64_t render_pass_generation{0};
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
            // The pass attachments the variant was built for, so the
            // debug hot reload can rebuild it for the same render pass.
            uint32_t color_count{1};
            VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        };
        std::vector<variant> graphics_variants;

        // Stored descriptor used to lazy-build the graphics
        // variants. Empty for compute pipelines.
        pipeline_descriptor descriptor;
    };

    struct vk_bind_group
    {
        bind_group_layout layout{};
        // One descriptor set per frame slot when any buffer the group
        // binds is multi-buffered (see vk_buffer::region_count): set
        // @c s points at region @c s of each such buffer, and a bind
        // picks the set of the current frame slot. Otherwise a single
        // set, which every slot binds. The sets are written once, at
        // creation, and never updated.
        std::array<VkDescriptorSet, k_max_frames_in_flight> descriptor_sets{};
        uint32_t set_count{0};
        // The pool of the device's grow-on-demand chain each set was
        // allocated from (the chain may grow between two sets of one
        // group); a set is only ever freed back to its own pool.
        std::array<VkDescriptorPool, k_max_frames_in_flight> pools{};
        std::vector<binding_value> entries;
        // Dynamic uniform-buffer slots of the layout the set was
        // allocated with: how many offsets every bind must pass. Kept
        // here because the layout may be destroyed before the group.
        uint32_t dynamic_count{0};

        // The set to bind for frame slot @p slot; null for a group whose
        // allocation failed or that was destroyed.
        VkDescriptorSet descriptor_set(uint32_t slot) const noexcept
        {
            if (set_count == 0)
            {
                return VK_NULL_HANDLE;
            }
            return descriptor_sets[set_count == 1 ? 0 : slot % set_count];
        }
    };

    // One attachment of an off-screen target: the texture it renders
    // into (allocated by the device and released with the target when
    // @c owned, otherwise imported and left to its owner) and the
    // level / layer it is attached at.
    struct vk_attachment
    {
        texture tex{};
        bool owned{false};
        uint32_t mip_level{0};
        uint32_t layer{0};
    };

    // What distinguishes one VkRenderPass of a target from another:
    // the load / store op of every attachment and whether the depth
    // attachment takes part at all. Two passes over the same target
    // with equal keys share the render pass and its framebuffers.
    struct vk_render_pass_key
    {
        std::array<VkAttachmentLoadOp, max_color_attachments> color_load{};
        std::array<VkAttachmentStoreOp, max_color_attachments> color_store{};
        VkAttachmentLoadOp depth_load{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
        VkAttachmentStoreOp depth_store{VK_ATTACHMENT_STORE_OP_STORE};
        bool use_depth{false};

        bool operator==(const vk_render_pass_key&) const = default;
    };

    struct vk_render_target
    {
        // Render-pass variants keyed by their @ref vk_render_pass_key.
        // The same target is used by passes that disagree on load-op
        // (the swapchain hosts tonemap with LOAD_OP_CLEAR followed by
        // ui with LOAD_OP_LOAD); each unique key gets its own
        // VkRenderPass and its own set of framebuffers, and none are
        // destroyed on a load-op switch. Variants with different
        // use_depth are not render-pass compatible (different
        // attachment counts), so the framebuffers live on the variant
        // rather than being shared across variants.
        //
        // Variants are only ever retired wholesale — when the
        // swapchain is rebuilt (its framebuffers point at the old
        // images) or the target is destroyed — and retirement goes
        // through @c vk_device::retire_render_pass_variants, which
        // also purges every pipeline variant built against the
        // retired passes. The pipeline cache keys on the generation
        // stamped here, not on the VkRenderPass handle alone, so a
        // recycled handle value can never resurrect a stale entry.
        struct variant
        {
            vk_render_pass_key key{};
            VkRenderPass render_pass{VK_NULL_HANDLE};
            // Device-wide, monotonically increasing; assigned when
            // the render pass is created and never reused.
            uint64_t render_pass_generation{0};
            // Colour attachments the pass writes: what a pipeline
            // built against it needs a blend state for.
            uint32_t color_count{0};
            // Per-swapchain-image for swapchain targets; one entry
            // for off-screen targets.
            std::vector<VkFramebuffer> framebuffers;
        };
        std::vector<variant> variants;

        uint32_t width{0};
        uint32_t height{0};
        uint32_t samples{1};
        bool has_depth{true};
        // The depth attachment's format carries a stencil plane (or
        // the swapchain depth does), so it clears and stores with it.
        bool has_stencil{false};

        // Colour attachments in location order, empty for the
        // swapchain (whose one colour plane is the acquired image) and
        // for a depth-only target. Exposed for next-pass sampling
        // through @c render_target_color_texture.
        std::vector<vk_attachment> color;
        vk_attachment depth;

        bool is_swapchain{false};
    };

    // A pool of timestamp queries.
    struct vk_query_set
    {
        VkQueryPool pool{VK_NULL_HANDLE};
        uint32_t count{0};
    };
} // namespace rendering_engine::gpu::backend::vulkan
