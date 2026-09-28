// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file shadow_casters.hpp
 * @brief The instanced depth-only pipeline and the caster dispatch shared
 *        by the directional and omni shadow passes.
 */

#pragma once

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine::gpu
{
    struct device;
    struct render_pass_encoder;
} // namespace rendering_engine::gpu

namespace rendering_engine
{
    /**
     * @brief The instanced twin of a shadow pass's depth-only pipeline.
     *
     * The single-draw shadow pipeline takes each caster's model matrix from
     * the PerDraw block its draw pushes or binds at slot 1. An instanced
     * draw has none: its transforms travel in the per-instance vertex
     * stream it binds at vertex slot 1 (four vec4 model columns, then a
     * tint; see @ref mesh_instance) and it draws indexed-indirect. The instanced pipeline declares no push-constant
     * range, which is fine because the dispatch rebinds the light group on
     * every pipeline switch. This
     * pipeline reads the same stream through
     * @c shaders/passes/shadow_instanced.vert.glsl, so instanced batches
     * cast with the same light view-projection and fixed-function state as
     * every other caster.
     */
    struct instanced_shadow_pipeline
    {
        gpu::shader_module vertex_shader{};
        gpu::pipeline pipeline{};
    };

    // Builds the instanced pipeline on @p device (vertex stage only: the
    // depth-only target has no colour attachment) over @p light_layout at
    // slot 0 and the given depth / blend / rasterizer / depth-bias state,
    // which the caller passes unchanged from its single-draw pipeline so
    // both rasterize identically.
    instanced_shadow_pipeline create_instanced_shadow_pipeline(gpu::device& device,
                                                               gpu::bind_group_layout light_layout,
                                                               const gpu::depth_state& depth,
                                                               const gpu::blend_state& blend,
                                                               const gpu::rasterizer_state& rasterizer,
                                                               const gpu::depth_bias_state& depth_bias);

    // Releases the pipeline and its vertex shader on @p device, the one
    // they were built on; no-op for invalid handles.
    void destroy_instanced_shadow_pipeline(gpu::device& device, instanced_shadow_pipeline& instanced);

    /**
     * @brief Records caster draw items into an open shadow render pass.
     *
     * Picks the single-draw or the instanced pipeline per item (an item
     * with a per-instance stream is an instanced batch), binding the light
     * bind group at slot 0 again whenever the pipeline changes, and issues
     * the matching draw — indexed-indirect when the item carries an
     * indirect command, else with the item's own counts and geometry
     * offsets. A single draw pushes its PerDraw block (@ref push_per_draw),
     * the very block the scene pass does. Items that carry neither a
     * PerDraw block nor an instance stream have no model matrix to cast
     * with and are skipped.
     */
    class shadow_caster_dispatch
    {
    public:
        shadow_caster_dispatch(gpu::render_pass_encoder& encoder,
                               gpu::pipeline single_pipeline,
                               gpu::pipeline instanced_pipeline,
                               gpu::bind_group light_bind_group);

        void draw(const draw_item& item);

    private:
        gpu::render_pass_encoder& m_encoder;
        gpu::pipeline m_single_pipeline{};
        gpu::pipeline m_instanced_pipeline{};
        gpu::bind_group m_light_bind_group{};

        // The pipeline currently set on the encoder; invalid until the
        // first draw.
        gpu::pipeline m_bound_pipeline{};
    };
} // namespace rendering_engine
