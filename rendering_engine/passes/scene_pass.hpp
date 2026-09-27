// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_stats.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;

    /**
     * @brief 3D scene pass. Clears the HDR scene colour and depth (or
     *        loads the depth the @ref depth_prepass laid down this frame),
     *        layer-filters and frustum-culls the scene-renderable
     *        registry against the camera, collects draw items from the
     *        survivors, sorts them by render queue / depth / pipeline via
     *        @ref draw_item::sort_key, and dispatches them. Every draw
     *        reaches the pass as a registered renderable's
     *        @ref draw_item; @ref record runs no event listener.
     *
     * The per-frame uploads, the sorted draw list and the pipeline every
     * item binds are built once per frame by @ref prepare; @ref record
     * (and @ref record_depth_prepass, which the depth pre-pass calls
     * ahead of it) then only encode from that list, so both passes draw
     * the same items with the same per-frame and per-draw data. The
     * depth pre-pass announces the frames it runs through
     * @ref expect_depth_prepass before this pass prepares: the pass then
     * resolves each pre-passed item's depth-only twin
     * (@ref material::depth_prepass_pipeline) for it, loads the scene
     * depth rather than clearing it and draws every pre-passed item
     * (@ref material::draws_in_depth_prepass) with its
     * @ref material::depth_prepassed_pipeline — depth writes off, a
     * less-or-equal test — so each covered pixel shades once; the other
     * items keep their ordinary variant.
     *
     * A frame whose draw list is longer than the parallel draw threshold
     * (@c core::graphics_settings::parallel_draw_threshold) is recorded
     * in parallel: the sorted list is cut into contiguous chunks of at
     * least that many draws, at most one per recording thread (the job
     * pool's workers plus the main thread), the pass is begun with
     * @c render_pass_descriptor::parallel and every chunk is dispatched
     * into a secondary encoder of its own on a worker, in the list's
     * order, while the main thread helps and waits; the primary then
     * executes the secondaries in order, so the frame's draws land
     * exactly as the serial walk would issue them. Each chunk binds the
     * per-frame group and its pipelines itself, since bound state does
     * not carry into a secondary. Below the threshold, with it at 0 or
     * without workers the list is dispatched serially on the primary.
     * The shading pass and the depth pre-pass both take this path; the
     * render stats are tallied from the list in @ref prepare, so they are
     * the same either way.
     *
     * A renderable whose @ref renderable::layer_mask shares no bit with
     * the camera's @ref camera::culling_mask is skipped outright. Culling
     * then asks each survivor for its @ref renderable::world_bounds
     * before @ref renderable::collect_draw_items and skips those that
     * lie wholly outside the camera frustum, so they never build an item;
     * a renderable that reports no bounds is always collected. The tallies
     * land in @ref render_stats::submitted / @ref render_stats::culled.
     * Each survivor's items are then keyed by @ref make_sort_key from the
     * item's material queue (opaque or transparent), the view-space depth
     * to the renderable's bounds centre, and the item's pipeline, so the
     * final sort draws opaque geometry front-to-back and transparent
     * geometry back-to-front.
     *
     * Owns the per-frame bind-group layout (the @ref view_globals block at
     * binding 0 and the packed lights block at binding 2, both in slot 0;
     * plus the directional cascade block and its depth array with the
     * comparison sampler, and the omni and spot shadow blocks and maps, at
     * the numbers in gpu/shader_bindings.hpp).
     * The matching lit materials read the layout via
     * @ref frame_bind_group_layout so the pipeline and the runtime bind
     * group agree on slot shape.
     *
     * Skipped when no camera is attached.
     */
    struct scene_pass : pass
    {
        // The shadow passes that run ahead of this one reach it through the
        // frame context (@ref frame_context::directional_shadow,
        // @c point_shadow, @c spot_shadow): their maps (and the directional
        // comparison sampler) go into the per-frame bind group, rebuilt if
        // one of them changes, and their fitted matrices, biases and
        // caster indices are uploaded each frame so the lit materials can
        // sample them. An absent shadow pass disables that kind of
        // shadowing.
        // @p stats is filled with this frame's draw statistics each record();
        // non-owning, owned by the renderer and surfaced to the debug
        // overlay. May be null to disable stats collection.
        // @p taa_jitter says whether the renderer will publish a temporal-AA
        // jitter through @ref frame_context::jitter: the pass then applies
        // it to the projection it uploads and builds the unjittered overlay
        // twin of its per-frame bind group (see @ref overlay_frame_bind_group).
        // @p parallel_draw_threshold is the draw count above which a frame
        // is recorded in parallel, and the fewest draws per chunk (see the
        // class comment); 0 keeps every frame serial.
        scene_pass(const std::vector<renderable*>* registry,
                   render_stats* stats,
                   bool taa_jitter,
                   uint32_t parallel_draw_threshold);
        ~scene_pass() override;

        scene_pass(const scene_pass&) = delete;
        scene_pass& operator=(const scene_pass&) = delete;

        // Builds this frame's state: resets the stats, (re)builds the
        // per-frame groups, uploads the per-frame blocks (view_globals,
        // lights, the shadow blocks — from the shadow passes, which
        // prepared ahead of this one), collects, keys and sorts the draw
        // list and resolves the pipeline every item binds in each of the
        // two dispatches. With no camera it only resets the stats and
        // leaves the list empty. Consumes the depth pre-pass's
        // @ref expect_depth_prepass for the frame.
        void prepare(const frame_context& ctx) override;

        // Opens the scene pass over the HDR target and dispatches the
        // list @ref prepare built (in parallel above the threshold).
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "scene";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read("shadow_map");
            io.read("point_shadow");
            io.read("spot_shadow");
            // Loaded rather than cleared on frames the depth pre-pass ran.
            io.read("scene_depth");
            io.write("scene_color");
            io.write("scene_depth");
        }

        // Called by the @ref depth_prepass from its own prepare, which the
        // pass list runs right before this pass's, on a frame it will lay
        // the opaque depth down: this pass's @ref prepare then resolves the
        // pre-passed items' depth-only twins and @ref record loads the
        // depth and shades those items with their depth-prepassed
        // variants. Holds for the next @ref prepare only.
        void expect_depth_prepass();

        // Draws this frame's pre-passed items (the opaque queue, filtered
        // by @ref material::draws_in_depth_prepass, front-to-back) through
        // their @ref material::depth_prepass_pipeline into a pass begun
        // on @p encoder with @p descriptor, a depth-only pass the
        // @ref depth_prepass describes over the scene depth attachment;
        // this pass begins and ends it, since above the threshold it is
        // begun for parallel recording and the items are dispatched from
        // worker threads like the shading pass's. Call after @ref prepare,
        // on a frame announced through @ref expect_depth_prepass.
        void record_depth_prepass(gpu::command_encoder& encoder, const gpu::render_pass_descriptor& descriptor);

        // Layout for the per-frame bind group bound at slot 0 each
        // frame. The matching material's pipeline_descriptor must
        // reserve slot 0 for this layout.
        gpu::bind_group_layout frame_bind_group_layout() const;

        // The per-frame bind group itself (camera / lights / shadow at
        // slot 0). Built by the first @ref prepare and stable after that
        // unless a shadow map changes — the pass refills the backing UBOs
        // every frame rather than recreating the group. Passes later in the
        // frame (the volumetric fog) read it through
        // @ref frame_context::scene once this pass has prepared, and bind it
        // to draw with the same camera, lights and shadows the scene used.
        gpu::bind_group frame_bind_group() const;

        // Per-frame bind group carrying the *unjittered* camera, for
        // consumers that draw after the TAA resolve (the debug pass) and so
        // would otherwise show the projection jitter as an un-averaged
        // sub-pixel wobble. Identical to @ref frame_bind_group in every
        // other binding, and the same handle when temporal-AA jitter is off
        // (there is nothing to undo). Built and rebuilt with it; the debug
        // pass reads it through @ref frame_context::scene.
        gpu::bind_group overlay_frame_bind_group() const;

        // No resize override: the pass renders into the scene target the
        // renderer hands it each frame, and the jitter it applies arrives
        // through frame_context already scaled to the live target size.

    private:
        // Which of the two passes a @ref dispatch walk records for.
        enum class draw_phase
        {
            // The depth pre-pass: pre-passed items only, depth-only
            // variants.
            depth_prepass,
            // This pass: every item, pre-passed ones through their
            // depth-prepassed variants when the pre-pass ran this frame.
            shading,
        };

        // The pipelines one item of @ref m_items binds, resolved by
        // @ref prepare in the list's order (@ref m_pipelines): the one the
        // shading pass draws it with (its ordinary variant, or the
        // depth-prepassed twin on a pre-passed frame) and the depth-only
        // twin the pre-pass draws it with — invalid for an item the
        // pre-pass skips.
        struct item_pipelines
        {
            gpu::pipeline shading{};
            gpu::pipeline depth{};
        };

        // (Re)builds @ref m_frame_bind_group and its overlay twin when they do
        // not exist yet or a shadow map @p ctx publishes differs from the one
        // they were built with. Called by @ref prepare.
        void update_frame_bind_groups(const frame_context& ctx);

        // The chunks a dispatch of @p draw_count items is cut into: 1 (a
        // serial walk on the primary) at or below the threshold, with the
        // threshold at 0, without a parallel-recording device or without
        // workers; otherwise draw_count / threshold contiguous chunks, at
        // least two and at most one per recording thread.
        uint32_t plan_chunks(size_t draw_count) const;

        // Begins @p descriptor's pass on @p encoder, dispatches the
        // @p phase's share of @ref m_items — serially, or in parallel
        // through one secondary encoder per chunk (see the class comment)
        // — and ends the pass.
        void record_phase(gpu::command_encoder& encoder, gpu::render_pass_descriptor descriptor, draw_phase phase);

        // Binds and draws @ref m_items in [@p first, @p last) for @p phase
        // into @p pass_encoder: the per-frame group once, the pipeline
        // when it changes, the per-material group when the instance
        // changes, then each item's per-draw block, vertex / index streams
        // and draw call. Reads only what @ref prepare left, so several
        // chunks run on several threads at once.
        void dispatch(gpu::render_pass_encoder& pass_encoder, draw_phase phase, size_t first, size_t last) const;

        // Non-owning back-pointer to the render world's
        // scene-renderable registry. The world outlives every pass
        // (see renderer.hpp), so the pointer stays valid for the pass's
        // lifetime.
        const std::vector<renderable*>* m_registry;

        // Per-frame state — owned by the pass; the layout and buffers are
        // created at construction, the groups by the first prepare(), and
        // the buffers refilled every prepare(). Released in the destructor before
        // the device tears its pools down. The frame UBO carries the
        // @ref view_globals block (camera matrices, viewport, clock,
        // jitter and fog) at binding 0; the lights UBO carries the
        // packed @ref gpu_lights block at binding 2. Both live in the
        // single per-frame bind group bound at slot 0.
        gpu::bind_group_layout m_frame_layout{};
        gpu::buffer m_frame_ubo{};
        gpu::buffer m_lights_ubo{};
        gpu::buffer m_shadow_ubo{};
        gpu::buffer m_point_shadow_ubo{};
        gpu::buffer m_spot_shadow_ubo{};
        gpu::bind_group m_frame_bind_group{};

        // Unjittered twin of @ref m_frame_bind_group for the debug pass.
        // Only created when temporal-AA jitter is active; otherwise the
        // accessor hands back the main group (the matrices are identical).
        // Shares every other binding with the main group — only its
        // view_globals block differs, describing the view without the
        // sub-pixel offset.
        gpu::buffer m_overlay_frame_ubo{};
        gpu::bind_group m_overlay_frame_bind_group{};

        // The shadow maps and sampler the groups above were built with, as
        // the frame context published them (invalid for an absent shadow
        // pass); @ref update_frame_bind_groups rebuilds when they differ.
        gpu::texture m_bound_shadow_map{};
        gpu::sampler m_bound_shadow_sampler{};
        gpu::texture m_bound_point_shadow_map{};
        gpu::texture m_bound_spot_shadow_map{};

        // Non-owning; filled each prepare() with this frame's draw stats.
        // Owned by the renderer, which outlives the pass. Null
        // disables collection.
        render_stats* m_stats{nullptr};

        // Temporal-AA projection jitter. When set, each prepare() offsets
        // the camera projection by the sub-pixel jitter the renderer
        // published in frame_context::jitter (a Halton step computed from
        // the live target size) before uploading it, so consecutive frames
        // sample the scene at slightly different positions for the
        // @ref taa_pass to accumulate. Decided at construction because the
        // overlay bind group only exists when jitter runs.
        bool m_taa_jitter{false};

        // Reused across frames so the underlying allocation persists.
        // The sorted list and, in the same order, the pipelines each item
        // binds; @ref m_depth_item_end is one past the last item the
        // pre-pass draws (the list sorts the opaque queue first, so the
        // pre-passed items are a prefix bar the ones their material opts
        // out of). Written by @ref prepare, read by the dispatches.
        std::vector<draw_item> m_items;
        std::vector<item_pipelines> m_pipelines;
        size_t m_depth_item_end{0};

        // Draw count above which a dispatch is recorded in parallel and
        // the fewest draws per chunk (0: never); fixed at construction.
        uint32_t m_parallel_draw_threshold{0};

        // The depth pre-pass announced itself for the next @ref prepare
        // (@ref expect_depth_prepass), and whether it runs this frame,
        // consumed from that by @ref prepare: picks load vs clear and the
        // pre-passed variants.
        bool m_depth_prepass_requested{false};
        bool m_depth_prepassed{false};
    };
} // namespace rendering_engine
