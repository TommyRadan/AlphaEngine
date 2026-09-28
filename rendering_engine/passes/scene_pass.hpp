// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_stats.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace core
{
    struct job_pool;
}

namespace rendering_engine
{
    /**
     * @brief 3D scene pass. Clears the HDR scene colour and depth (or
     *        loads the depth the @ref depth_prepass laid down this frame),
     *        layer-filters and frustum-culls the frame's mesh draws
     *        (@ref frame_context::scene_draws, one per mesh proxy) against
     *        the camera, collects the survivors' draw items, sorts them by
     *        render queue / depth / pipeline via @ref draw_item::sort_key,
     *        and dispatches them. Every draw reaches the pass as a mesh
     *        proxy's @ref draw_item; @ref record runs no event listener.
     *
     * The per-frame uploads, the sorted draw list and the pipeline every
     * item binds are built once per frame by @ref prepare; @ref record
     * (and @ref record_depth_prepass, which the depth pre-pass calls
     * ahead of it through the published @ref scene_view_data) then only
     * encode from that list, so both passes draw the same items with the
     * same per-frame and per-draw data. The depth pre-pass publishes
     * @ref frame_resources::depth_prepass on the frames it runs, before
     * this pass prepares: the pass then resolves each pre-passed item's
     * depth-only twin (@ref material::depth_prepass_pipeline) for it,
     * loads the scene depth rather than clearing it and draws every
     * pre-passed item (@ref material::draws_in_depth_prepass) with its
     * @ref material::depth_prepassed_pipeline — depth writes off, a
     * less-or-equal test — so each covered pixel shades once; the other
     * items keep their ordinary variant.
     *
     * A frame whose draw list is longer than the parallel draw threshold
     * (@c rendering_engine::graphics_settings::parallel_draw_threshold) is recorded
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
     * A mesh draw whose @ref mesh_draw::layer_mask shares no bit with the
     * camera's culling mask is skipped outright. Culling then skips those
     * whose @ref mesh_draw::bounds lie wholly outside the camera frustum; a
     * draw without bounds is always collected. The tallies land in
     * @ref render_stats::submitted / @ref render_stats::culled. Each
     * survivor's item is then keyed by @ref make_sort_key from the item's
     * material queue (opaque or transparent), the view-space depth to the
     * draw's bounds centre, and the item's pipeline, so the final sort
     * draws opaque geometry front-to-back and transparent geometry
     * back-to-front.
     *
     * Owns the per-frame bind-group layout (the @ref view_globals block at
     * binding 0 and the packed lights block at binding 2, both in slot 0;
     * plus the directional cascade block and its depth array with the
     * comparison sampler, and the omni and spot shadow blocks and maps, at
     * the numbers in gpu/shader_bindings.hpp).
     * The matching lit materials read the layout via
     * @ref frame_bind_group_layout so the pipeline and the runtime bind
     * group agree on slot shape. The view_globals block and the groups over
     * it are the view's (kept in its resource set); the lights and shadow
     * blocks are the same for every view of a frame and shared. Every
     * frame the pass publishes, per view, the layout and the view's groups
     * as @ref frame_resources::scene_view, for the passes
     * that draw with the scene's camera, lights and shadows later in the
     * frame (the volumetric fog, the debug pass) and for the depth
     * pre-pass, which records this pass's list through it.
     *
     * Draws into @ref frame_resources::scene_color and its depth; skipped
     * when no camera is attached.
     */
    struct scene_pass : pass, depth_prepass_source
    {
        // The shadow passes that run ahead of this one publish their maps
        // and fits (@ref frame_resources::directional_shadow,
        // @c point_shadow, @c spot_shadow): the maps (and the directional
        // comparison sampler) go into the per-frame bind group, rebuilt if
        // one of them changes, and the fitted matrices, biases and caster
        // indices are uploaded each frame so the lit materials can sample
        // them. A shadow that is not published disables that kind of
        // shadowing.
        // @p stats is filled with this frame's draw statistics each record();
        // non-owning, owned by the renderer and surfaced to the debug
        // overlay. May be null to disable stats collection.
        // @p taa_jitter says whether the renderer will publish a temporal-AA
        // jitter through @ref frame_context::jitter: the pass then applies
        // it to the projection it uploads and builds the unjittered overlay
        // twin of each view's per-frame bind group (see @ref view_data).
        // @p parallel_draw_threshold is the draw count above which a frame
        // is recorded in parallel, and the fewest draws per chunk (see the
        // class comment); 0 keeps every frame serial. The chunks record on
        // @p jobs, whose workers set how many there are; null keeps every
        // frame serial too.
        scene_pass(gpu::device& device,
                   core::job_pool* jobs,
                   render_stats* stats,
                   bool taa_jitter,
                   uint32_t parallel_draw_threshold);
        ~scene_pass() override;

        scene_pass(const scene_pass&) = delete;
        scene_pass& operator=(const scene_pass&) = delete;

        // Builds this frame's state: resets the stats, (re)builds and
        // publishes the per-frame groups, uploads the per-frame blocks
        // (view_globals, lights, the shadow blocks — from the shadow
        // passes, which prepared ahead of this one), collects, keys and
        // sorts the draw list and resolves the pipeline every item binds
        // in each of the two dispatches, the depth pre-pass's included when
        // it published @ref frame_resources::depth_prepass. With no camera
        // it only resets the stats and leaves the list empty.
        void prepare(const frame_context& ctx) override;

        // Opens the scene pass over the HDR target and dispatches the
        // list @ref prepare built (in parallel above the threshold).
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::scene;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read_optional(frame_resources::directional_shadow);
            io.read_optional(frame_resources::point_shadow);
            io.read_optional(frame_resources::spot_shadow);
            // Loaded rather than cleared on frames the depth pre-pass ran.
            io.read_optional(frame_resources::depth_prepass);
            io.read_optional(frame_resources::scene_depth);
            io.write(frame_resources::scene_color);
            io.write(frame_resources::scene_depth);
            io.write(frame_resources::scene_view);
        }

        // Draws this frame's pre-passed items (the opaque queue, filtered
        // by @ref material::draws_in_depth_prepass, front-to-back) through
        // their @ref material::depth_prepass_pipeline into a pass begun
        // on @p encoder with @p descriptor, a depth-only pass the
        // @ref depth_prepass describes over the scene depth attachment;
        // this pass begins and ends it, since above the threshold it is
        // begun for parallel recording and the items are dispatched from
        // worker threads like the shading pass's. Called by the depth
        // pre-pass while it records, on a frame it published
        // @ref frame_resources::depth_prepass for.
        void record_depth_prepass(gpu::command_encoder& encoder,
                                  const gpu::render_pass_descriptor& descriptor) const override;

        // Layout for the per-frame bind group bound at slot 0 each
        // frame. The matching material's pipeline_descriptor must
        // reserve slot 0 for this layout.
        gpu::bind_group_layout frame_bind_group_layout() const;

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // The pool a parallel dispatch records its chunks on (null: none);
        // handed in by the renderer and outlives the pass.
        core::job_pool* m_jobs{nullptr};

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

        // What the pass keeps per view: the view_globals block (camera
        // matrices, viewport, clock, jitter and fog) at binding 0, its
        // unjittered twin, and the per-frame groups built over them. The
        // twin exists only while temporal-AA jitter is active; it backs the
        // group passes that draw after the TAA resolve (the debug pass)
        // bind, which would otherwise show the projection jitter as an
        // un-averaged sub-pixel wobble, and shares every other binding with
        // the main group. The shadow maps and sampler the groups were built
        // with, as the shadow passes published them (invalid for a shadow
        // that is not published), are kept so @ref update_frame_bind_groups
        // rebuilds when they differ.
        struct view_data final : pass_view_state
        {
            view_data(gpu::device& device, bool taa_jitter);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            gpu::device* device{nullptr};
            gpu::buffer frame_ubo{};
            gpu::buffer overlay_frame_ubo{};
            gpu::bind_group frame_bind_group{};
            gpu::bind_group overlay_frame_bind_group{};

            gpu::texture bound_shadow_map{};
            gpu::sampler bound_shadow_sampler{};
            gpu::texture bound_point_shadow_map{};
            gpu::texture bound_spot_shadow_map{};
        };

        // (Re)builds @p view's per-frame group and its overlay twin when
        // they do not exist yet or a shadow map differs from the one they
        // were built with; a null shadow binds nothing for that kind.
        // Called by @ref prepare.
        void update_frame_bind_groups(view_data& view,
                                      const directional_shadow_data* shadow,
                                      const point_shadow_data* point_shadow,
                                      const spot_shadow_data* spot_shadow);

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
        void
        record_phase(gpu::command_encoder& encoder, gpu::render_pass_descriptor descriptor, draw_phase phase) const;

        // Binds and draws @ref m_items in [@p first, @p last) for @p phase
        // into @p pass_encoder: the per-frame group once, the pipeline
        // when it changes, the per-material group when the instance
        // changes, then each item's per-draw block, vertex / index streams
        // and draw call. Reads only what @ref prepare left, so several
        // chunks run on several threads at once.
        void dispatch(gpu::render_pass_encoder& pass_encoder, draw_phase phase, size_t first, size_t last) const;

        // The per-frame layout, created at construction, and the blocks
        // every view's group shares, refilled by every prepare() with what
        // is the same for every view of a frame: the packed @ref gpu_lights
        // block at binding 2 and the three shadow blocks. Released in the
        // destructor before the device tears its pools down.
        gpu::bind_group_layout m_frame_layout{};
        gpu::buffer m_lights_ubo{};
        gpu::buffer m_shadow_ubo{};
        gpu::buffer m_point_shadow_ubo{};
        gpu::buffer m_spot_shadow_ubo{};

        // The per-frame group of the view being rendered, set by
        // @ref prepare for the dispatches that follow.
        gpu::bind_group m_frame_bind_group{};

        // Non-owning; filled each prepare() with this frame's draw stats.
        // Owned by the renderer, which outlives the pass. Null
        // disables collection.
        render_stats* m_stats{nullptr};

        // Temporal-AA projection jitter. When set, each prepare() offsets
        // the camera projection by the sub-pixel jitter the renderer
        // published in frame_context::jitter (a Halton step computed from
        // the view's size) before uploading it, so consecutive frames
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

        // The scene colour target this frame draws into, looked up by
        // @ref prepare.
        gpu::render_target m_target{};

        // Whether the depth pre-pass runs this frame (it published
        // @ref frame_resources::depth_prepass), decided by @ref prepare:
        // picks load vs clear and the pre-passed variants.
        bool m_depth_prepassed{false};
    };
} // namespace rendering_engine
