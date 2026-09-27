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

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>
#include <rendering_engine/render_stats.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;
    struct shadow_pass;
    struct point_shadow_pass;
    struct spot_shadow_pass;

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
     * The per-frame uploads and the sorted draw list are built once per
     * frame by @ref prepare, which the depth pre-pass calls first when it
     * runs, so both passes draw the same items with the same per-frame
     * and per-draw data. On such a frame the pass loads the scene depth
     * rather than clearing it and draws every pre-passed item
     * (@ref material::draws_in_depth_prepass) with its
     * @ref material::depth_prepassed_pipeline — depth writes off, a
     * less-or-equal test — so each covered pixel shades once; the other
     * items keep their ordinary variant.
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
     * Skipped when no camera is attached (matches the previous
     * @c if (camera != nullptr) gate).
     */
    struct scene_pass : pass
    {
        // @p shadow is the directional shadow pass that runs ahead of this
        // one; the scene pass bakes its cascade array and comparison
        // sampler into the per-frame bind group and uploads its cascade
        // matrices and splits each frame so the lit materials can sample
        // it. May be null to disable shadowing.
        // @p point_shadow is the omni shadow pass for the first shadow-casting
        // point light; its six depth maps are baked into the per-frame bind
        // group and its matrices uploaded each frame. May be null.
        // @p spot_shadow is the shadow pass for the first shadow-casting spot
        // light; its depth map is baked into the per-frame bind group and its
        // matrix uploaded each frame. May be null.
        // @p stats is filled with this frame's draw statistics each record();
        // non-owning, owned by the engine context and surfaced to the debug
        // overlay. May be null to disable stats collection.
        // @p taa_jitter says whether the context will publish a temporal-AA
        // jitter through @ref frame_context::jitter: the pass then applies
        // it to the projection it uploads and builds the unjittered overlay
        // twin of its per-frame bind group (see @ref overlay_frame_bind_group).
        scene_pass(std::vector<renderable*>* registry,
                   shadow_pass* shadow,
                   point_shadow_pass* point_shadow,
                   spot_shadow_pass* spot_shadow,
                   render_stats* stats,
                   bool taa_jitter);
        ~scene_pass() override;

        scene_pass(const scene_pass&) = delete;
        scene_pass& operator=(const scene_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "scene";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.read("shadow_map");
            io.read("point_shadow");
            io.read("spot_shadow");
            // Loaded rather than cleared on frames the depth pre-pass ran.
            io.read("scene_depth");
            io.write("scene_color");
            io.write("scene_depth");
        }

        // Builds this frame's state once: resets the stats, uploads the
        // per-frame blocks (view_globals, lights, the shadow blocks) and
        // collects, keys and sorts the draw list. A second call for the
        // same @ref frame_context::frame_index returns at once, so the
        // depth pre-pass can run it ahead of this pass's @ref record and
        // both passes see one list. Must run after the shadow passes have
        // recorded (their matrices and tallies feed the uploads and
        // stats). With no camera it only resets the stats.
        void prepare(const frame_context& ctx);

        // Draws this frame's pre-passed items (the opaque queue, filtered
        // by @ref material::draws_in_depth_prepass, front-to-back) through
        // their @ref material::depth_prepass_pipeline into @p pass_encoder,
        // a depth-only pass the @ref depth_prepass opened over the scene
        // depth attachment. Call after @ref prepare. Marks the frame as
        // pre-passed, so the following @ref record loads that depth and
        // draws those items with their depth-prepassed variants.
        void record_depth_prepass(gpu::render_pass_encoder& pass_encoder);

        // Layout for the per-frame bind group bound at slot 0 each
        // frame. The matching material's pipeline_descriptor must
        // reserve slot 0 for this layout.
        gpu::bind_group_layout frame_bind_group_layout() const;

        // The per-frame bind group itself (camera / lights / shadow at
        // slot 0). The handle is stable across frames — the pass refills
        // the backing UBOs in record() rather than recreating the group —
        // so the debug pass can capture it once and bind it to project
        // its line-based gizmos with the same camera the scene used.
        gpu::bind_group frame_bind_group() const;

        // Per-frame bind group carrying the *unjittered* camera, for
        // consumers that draw after the TAA resolve (the debug pass) and so
        // would otherwise show the projection jitter as an un-averaged
        // sub-pixel wobble. Identical to @ref frame_bind_group in every
        // other binding, and the same handle when temporal-AA jitter is off
        // (there is nothing to undo). Stable across frames.
        gpu::bind_group overlay_frame_bind_group() const;

        // No resize override: the pass renders into the scene target the
        // context hands it each frame, and the jitter it applies arrives
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

        // Binds and draws @ref m_items for @p phase into @p pass_encoder:
        // the per-frame group once, the pipeline when it changes, the
        // per-material group when the instance changes, then each item's
        // per-draw group, vertex / index streams and draw call.
        void dispatch(gpu::render_pass_encoder& pass_encoder, draw_phase phase);

        // Non-owning back-pointer to the engine context's
        // scene-renderable registry. The context outlives every
        // pass so the pointer stays valid for the pass's lifetime.
        std::vector<renderable*>* m_registry;

        // Per-frame state — owned by the pass; created once and
        // refilled every record(). Released in the destructor before
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

        // Shadow passes feeding the per-frame group. Non-owning — the
        // engine context owns the passes and orders them before this one.
        // Null disables that kind of shadowing.
        shadow_pass* m_shadow{nullptr};
        point_shadow_pass* m_point_shadow{nullptr};
        spot_shadow_pass* m_spot_shadow{nullptr};

        // Non-owning; filled each record() with this frame's draw stats.
        // Owned by the engine context, which outlives the pass. Null
        // disables collection.
        render_stats* m_stats{nullptr};

        // Temporal-AA projection jitter. When set, each record() offsets
        // the camera projection by the sub-pixel jitter the context
        // published in frame_context::jitter (a Halton step computed from
        // the live target size) before uploading it, so consecutive frames
        // sample the scene at slightly different positions for the
        // @ref taa_pass to accumulate. Decided at construction because the
        // overlay bind group only exists when jitter runs.
        bool m_taa_jitter{false};

        // Reused across frames so the underlying allocation persists.
        std::vector<draw_item> m_items;

        // The frame index @ref prepare last built @ref m_items for; empty
        // until the first frame.
        std::optional<uint64_t> m_prepared_frame;

        // Whether the depth pre-pass laid this frame's opaque depth down
        // (@ref record_depth_prepass ran since @ref prepare started the
        // frame). Picks load vs clear and the pre-passed variants.
        bool m_depth_prepassed{false};
    };
} // namespace rendering_engine
