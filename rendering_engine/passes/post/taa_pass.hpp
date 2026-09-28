// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Temporal anti-aliasing resolve on the tonemapped LDR image.
     *
     * The temporal partner to @ref fxaa_pass: where FXAA smooths a single
     * frame spatially, TAA accumulates many sub-pixel-jittered frames into
     * one stable, supersampled image. The scene pass jitters the
     * projection matrix by the Halton(2,3) offset the renderer publishes in
     * @ref frame_context::jitter each frame (gated on the same
     * @c temporal_aa setting), so every frame samples the scene at a
     * slightly different sub-pixel position; blending those frames over
     * time resolves detail that no single-frame filter can — fine geometry
     * edges, specular shimmer, the near-mirror IBL reflections. History
     * reprojection via per-pixel motion vectors keeps accumulated detail
     * aligned to surfaces, with colour clamping to suppress ghosting at
     * disocclusions.
     *
     * The resolve runs after @ref tonemap_pass on the LDR target (TAA on
     * the perceptual image keeps HDR fireflies from dominating the history)
     * and feeds @ref fxaa_pass, which closes the post chain. Each frame the
     * resolve stage reprojects the colour history along the per-pixel
     * motion vectors from @ref velocity_pass — sampling the history at
     * @c texCoord - velocity rather than the same pixel — so a moving
     * camera keeps the accumulated detail aligned to the surface instead of
     * smearing it. A 3x3 neighbourhood colour clamp then constrains that
     * reprojected history to the current frame's local min/max box before
     * the blend, suppressing the ghosting that reprojection alone leaves at
     * disocclusions; history that reprojects off-screen is dropped in
     * favour of the current frame.
     *
     * The history is a ping-pong pair of @c rgba8 targets: the frame's
     * resolve writes into one while sampling the other (last frame's
     * resolve) as history, and the roles swap after every frame, so no
     * copy is needed to store the history. The target being written this
     * frame is published as @ref frame_resources::taa_resolve so the next
     * pass (FXAA) samples it instead of the raw tonemap output; the handle
     * therefore alternates between two values from frame to frame (and
     * changes altogether when the view is resized), which FXAA absorbs with
     * a small per-handle bind-group cache.
     *
     * Every view accumulates a history of its own (see @ref view_data): the
     * pair, sized to the view, the params UBO and the bookkeeping live in
     * the view's resource set and go with the view. The history is only
     * meaningful for frames of the same camera, so the resolve restarts
     * from the current image alone (history weight 0) on the view's first
     * frame, on a no-camera frame, and after the view is resized — a camera
     * switch or teleport-by-reattach starts a new view and never blends
     * against a stale image. The reciprocal frame size the neighbourhood
     * taps step by is baked from the view's size, mirroring
     * @ref fxaa_pass, and rewritten when the view is resized, which also
     * recreates both targets. Pipeline state mirrors every other
     * fullscreen-triangle post pass (depth off, blend off, no culling,
     * single vec2 vertex attribute).
     */
    struct taa_pass : pass
    {
        // The two textures the resolve samples are not constructor inputs:
        // the tonemapped LDR image and the motion vectors are looked up
        // every frame (@ref frame_resources::ldr_color,
        // @ref frame_resources::velocity), and a view's resolve bind groups
        // are (re)built whenever either handle differs from the one they
        // were last built against.
        explicit taa_pass(gpu::device& device);
        ~taa_pass() override;

        taa_pass(const taa_pass&) = delete;
        taa_pass& operator=(const taa_pass&) = delete;

        // Sizes the view's pair to the view, restarts its history on a
        // camera-less frame, rebinds the inputs when their handles changed,
        // writes the feedback weight the frame needs, picks and publishes
        // the half this frame resolves into and swaps the pair's roles for
        // the view's next frame.
        void prepare(const frame_context& ctx) override;

        // Draws the resolve into the half @ref prepare picked.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::taa;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::ldr_color);
            io.read(frame_resources::velocity);
            // The history is last frame's resolve, which this pass keeps to
            // itself rather than publishing.
            io.read_unordered("taa_history");
            io.write(frame_resources::taa_resolve);
            io.write("taa_history");
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // One half of the ping-pong pair: the target, its colour texture,
        // and the resolve bind group that writes into it while sampling the
        // *other* half as history ({currentColor @0, historyColor @1,
        // velocity @2, params @3}).
        struct accumulation_target
        {
            gpu::render_target target{};
            gpu::texture texture{};
            gpu::bind_group resolve_bind_group{};
        };

        // What the pass keeps per view.
        struct view_data final : pass_view_state
        {
            explicit view_data(gpu::device& device);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            // (Re)allocates both rgba8 accumulation targets at @p width x
            // @p height — the new ones before the old ones are released, so
            // the published resolve handle changes and FXAA rebinds — drops
            // the resolve bind groups and the inputs they were built with,
            // notes the new texel step for the params UBO (the next
            // prepare() rewrites it, inside the frame bracket) and drops the
            // history: the next frame resolves from the current image alone.
            void resize(uint32_t width, uint32_t height);

            // Releases both resolve bind groups (no-op for invalid handles).
            void destroy_resolve_bind_groups();

            gpu::device* device{nullptr};

            // The resolve params UBO: {1/width, 1/height, feedback, 0}.
            gpu::buffer resolve_ubo{};

            // The pair and which half the next frame's resolve writes into
            // (the other half then holds the history); prepare() swaps the
            // roles after every frame. Both own their colour attachment, so
            // destroying the target releases the texture.
            std::array<accumulation_target, 2> targets{};
            uint32_t write_index{0};

            // The LDR and velocity textures the resolve bind groups were
            // built against; invalid until the first prepare() builds them,
            // and reset by resize() so they are rebuilt against the new
            // targets.
            gpu::texture bound_current{};
            gpu::texture bound_velocity{};

            // The view's size, and the per-texel step (1/width, 1/height)
            // baked from it, kept so prepare() can rewrite the params UBO —
            // bumping only the feedback weight — without losing the step in
            // xy.
            uint32_t width{0};
            uint32_t height{0};
            float inv_width{0.0f};
            float inv_height{0.0f};

            // The feedback weight the params UBO currently holds, or
            // negative when the UBO must be rewritten whatever the weight
            // (the texel step changed: a new or resized view). prepare()
            // compares the weight the frame needs against it and rewrites on
            // mismatch.
            float uploaded_feedback{-1.0f};

            // True while the history is unusable: before the view's first
            // frame, after a resize, and on a camera-less frame. While set,
            // the resolve uses the current frame only; the frame after
            // switches the params UBO to the steady-state feedback.
            bool first_frame{true};
        };

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_resolve_shader{};

        gpu::buffer m_vertex_buffer{};

        gpu::bind_group_layout m_resolve_layout{};
        gpu::pipeline m_resolve_pipeline{};

        // Rebuilds both of @p view's resolve bind groups against
        // @p current_color and @p velocity plus each half's opposite texture
        // as history and the params UBO, remembering the two input handles.
        void rebuild_resolve_bind_groups(view_data& view, gpu::texture current_color, gpu::texture velocity);

        // Writes {1/width, 1/height, feedback, 0} to @p view's params UBO
        // and remembers the feedback. Only called from prepare(), inside
        // the frame bracket: the buffer is host-mapped on a
        // deferred-execution backend and the previous frame may still be
        // reading it until begin_frame waits.
        static void write_params(view_data& view, float feedback);

        // The half this frame's record() draws into and its resolve bind
        // group, picked by prepare().
        gpu::render_target m_draw_target{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine
