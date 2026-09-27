// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file pipeline.hpp
 * @brief Pipeline state object — bakes shader, vertex layout, and fixed-
 *        function state at create time.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    // Blending and channel mask of one colour attachment. The same
    // factors apply to the colour and alpha channels.
    struct blend_state
    {
        bool enabled{false};
        blend_factor src{blend_factor::one};
        blend_factor dst{blend_factor::zero};
        blend_op op{blend_op::add};

        // Channels the pipeline writes; the rest keep the attachment's
        // previous value. Applies whether or not blending is enabled.
        color_write_mask write_mask{color_write_all};
    };

    struct depth_state
    {
        bool test_enabled{true};
        bool write_enabled{true};
        compare_function compare{compare_function::less};
    };

    // Stencil test and update for one face orientation.
    struct stencil_face_state
    {
        // The test between the reference value (set per pass through
        // @c render_pass_encoder::set_stencil_reference) and the
        // stored stencil value, both masked by @c stencil_state::read_mask.
        compare_function compare{compare_function::always};
        // Applied when the stencil test fails.
        stencil_op fail_op{stencil_op::keep};
        // Applied when the stencil test passes but the depth test fails.
        stencil_op depth_fail_op{stencil_op::keep};
        // Applied when both tests pass.
        stencil_op pass_op{stencil_op::keep};
    };

    // Stencil state of a pipeline. Only meaningful on a target whose
    // depth attachment carries a stencil plane (@c depth24_stencil8 or
    // the window backbuffer's stencil bits); off by default. The
    // reference value is dynamic so one pipeline serves many masks.
    struct stencil_state
    {
        bool test_enabled{false};
        stencil_face_state front;
        stencil_face_state back;
        uint32_t read_mask{0xFFu};
        uint32_t write_mask{0xFFu};
    };

    // Rasteriser depth bias, the usual fix for shadow acne: every
    // fragment's depth is offset by @c constant times the smallest
    // resolvable depth step plus @c slope times the polygon's depth
    // slope, and the sum is clamped to @c clamp when it is non-zero.
    // Maps to the Vulkan rasterisation state's depth bias (@c clamp
    // needs the @c depthBiasClamp feature and is 0 without it).
    struct depth_bias_state
    {
        bool enabled{false};
        float constant{0.0f};
        float slope{0.0f};
        float clamp{0.0f};
    };

    struct rasterizer_state
    {
        cull_mode cull{cull_mode::back};
        front_face front{front_face::counter_clockwise};
        polygon_mode polygon{polygon_mode::fill};
    };

    // Push-constant bytes every device takes at least: Vulkan's required
    // minimum of maxPushConstantsSize.
    constexpr uint32_t min_push_constants_size = 128;

    // A block of push constants a pipeline declares: @c size bytes from
    // byte @c offset, visible to @c stages. Both must be multiples of 4
    // and the block must end within @c device_limits::max_push_constants_size.
    // @c render_pass_encoder::push_constants writes into it, naming the
    // same stages. Maps to a @c VkPushConstantRange of the pipeline
    // layout.
    struct push_constant_range
    {
        shader_stages stages{shader_stages_default};
        uint32_t offset{0};
        uint32_t size{0};
    };

    // Pipeline state object: every piece of GPU state that a
    // backend can bake at create time lives here. Bound to a
    // @c render_pass_encoder by handle; binding does not consult any
    // string table.
    struct pipeline_descriptor
    {
        shader_module vertex_shader{};
        shader_module fragment_shader{};
        shader_module geometry_shader{};

        // Optional tessellation pair. Both must be either set or
        // both empty. Pipelines that bind tessellation must also
        // set @c topology to @c primitive_topology::patches and
        // give @c patch_control_points a non-zero value.
        shader_module tessellation_control_shader{};
        shader_module tessellation_evaluation_shader{};

        std::vector<vertex_buffer_layout> vertex_buffers;

        primitive_topology topology{primitive_topology::triangles};

        // Number of vertices per patch when @c topology is
        // @c patches. Maps to @c VkPipelineTessellationStateCreateInfo's
        // @c patchControlPoints. Ignored for non-patch topologies.
        uint32_t patch_control_points{0};

        // Blend state of every colour attachment the pipeline draws
        // into, unless @ref attachment_blend overrides one.
        blend_state blend;
        depth_state depth;
        rasterizer_state rasterizer;
        stencil_state stencil;
        depth_bias_state depth_bias;

        // Per-attachment overrides for a multiple-render-target pass:
        // entry @c i applies to colour attachment @c i, and attachments
        // past the vector's end fall back to @ref blend. Entries past
        // the target's attachment count are ignored. Differing entries
        // need @c device_features::independent_blend; a device without
        // it blends every attachment like attachment 0 (logged once).
        std::vector<blend_state> attachment_blend;

        // Samples per pixel the pipeline rasterises at. Must equal the
        // @c render_target_descriptor::sample_count of every target it
        // draws into; 1 for the single-sampled targets the engine uses.
        uint32_t sample_count{1};

        // Bind-group layouts referenced by this pipeline. The index
        // into this vector is the slot number passed to
        // @c render_pass_encoder::set_bind_group.
        std::vector<bind_group_layout> bind_group_layouts;

        // Push-constant blocks the pipeline's stages read. They are part
        // of the pipeline layout: on Vulkan a bind group bound under one
        // pipeline stays bound across a switch to another only when both
        // declare the same bind-group layouts up to that slot *and* the
        // same ranges, so pipelines a pass switches between under one
        // bound group should declare identical ranges, used or not.
        std::vector<push_constant_range> push_constant_ranges;
    };

    // Compute-pipeline descriptor. Returned as the same
    // @c pipeline handle as graphics pipelines but bound only
    // through @c compute_pass_encoder::set_pipeline.
    struct compute_pipeline_descriptor
    {
        shader_module compute_shader{};

        // Bind-group layouts referenced by this pipeline. The
        // index into this vector is the slot number passed to
        // @c compute_pass_encoder::set_bind_group.
        std::vector<bind_group_layout> bind_group_layouts;
    };
} // namespace rendering_engine::gpu
