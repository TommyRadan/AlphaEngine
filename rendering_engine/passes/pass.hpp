// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file pass.hpp
 * @brief Render-pass interface walked once per frame by the engine.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/post_settings.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct camera;
    struct render_world;
    struct scene_pass;
    struct shadow_pass;
    struct point_shadow_pass;
    struct spot_shadow_pass;

    /**
     * @brief Collects one pass's declared resource reads and writes.
     *
     * Passes name resources with stable strings (e.g. "scene_color") so they
     * never have to thread handles through their interfaces; the renderer's
     * @ref pass_list resolves the names when it validates the order. A pass
     * that declares nothing is an ordering-only entry — still recorded in
     * place, just invisible to the dependency check.
     */
    class pass_io_builder
    {
    public:
        void read(std::string_view resource)
        {
            m_reads.emplace_back(resource);
        }

        void write(std::string_view resource)
        {
            m_writes.emplace_back(resource);
        }

        const std::vector<std::string>& reads() const noexcept
        {
            return m_reads;
        }

        const std::vector<std::string>& writes() const noexcept
        {
            return m_writes;
        }

    private:
        std::vector<std::string> m_reads;
        std::vector<std::string> m_writes;
    };

    /**
     * @brief Per-frame state shared with every pass.
     *
     * Captured once at the top of @ref renderer::render so passes
     * cannot disagree about which camera or backbuffer is active
     * mid-frame, and so individual passes do not have to re-run the
     * camera arbitration on every entry.
     */
    struct frame_context
    {
        // Backbuffer the engine presents. The last post pass writes
        // here; the UI pass composites on top of it.
        gpu::render_target swapchain_target{};

        // The camera this frame renders with — the winner of @ref world's
        // arbitration (render_world::active_camera: the highest-priority
        // attached, enabled camera) evaluated once per frame — or nullptr
        // when no attached camera is enabled. Passes that need a camera
        // read it from here, never from @ref world directly, and
        // early-return when it is null.
        camera* active_camera{nullptr};

        // What this frame draws: the lights and cameras (@ref
        // render_world::lights, @ref render_world::cameras), the renderable
        // registries and the environment probe / fog. Passes read lights
        // and cameras only through this, never through a global, so more
        // than one render_world can exist in a process. Never null once
        // the renderer is up.
        const render_world* world{nullptr};

        // Pixel size of the off-screen scene / LDR targets (and so of
        // the swapchain they resolve into) this frame.
        uint32_t viewport_width{0};
        uint32_t viewport_height{0};

        // Frames rendered before this one since the renderer came up.
        // Drives the temporal-AA jitter sequence.
        uint64_t frame_index{0};

        // The engine clock (core::time) in seconds: the time since it
        // started and this frame's delta. The scene pass hands both to
        // shaders through the @ref view_globals block.
        float time_seconds{0.0f};
        float delta_seconds{0.0f};

        // Temporal-AA sub-pixel jitter for this frame, in NDC units
        // (see @ref taa_jitter_ndc), and the previous frame's. Zero
        // while temporal AA is off. The scene pass rasterises with the
        // projection offset by @c jitter (@ref jitter_projection) and the
        // skybox unprojects with the same offset so the two agree; the
        // velocity pass subtracts it to recover each pixel's unjittered
        // position. @c prev_jitter is the offset the history was
        // rasterised with, for consumers that relate two jittered frames.
        core::math::vec2 jitter{0.0f, 0.0f};
        core::math::vec2 prev_jitter{0.0f, 0.0f};

        // The previous frame's unjittered view-projection of
        // @ref active_camera, valid when @ref has_prev_view_projection
        // is set: false on the first camera frame, after a no-camera
        // frame and when a different camera won the arbitration, since a
        // matrix from a different camera (or none) is meaningless to
        // reproject against. The velocity pass builds its reprojection
        // from it.
        core::math::mat4 prev_view_projection{};
        bool has_prev_view_projection{false};

        // Off-screen HDR colour target the scene pass renders into.
        // Owned by @ref renderer; surfaced here so passes share the
        // handle without reaching back through the engine. Post
        // passes sample @ref scene_color_texture as their input.
        gpu::render_target scene_color_target{};
        gpu::texture scene_color_texture{};

        // Depth attachment of @ref scene_color_target, sampleable as a
        // shader input once the scene and skybox passes are done with
        // it. Re-read from the target every frame (not cached) so a
        // later resize that swaps the attachment reaches every pass;
        // consumers compare the handle against the one their bind group
        // was built with in their prepare and rebuild on change (see
        // velocity_pass).
        // Passes that sample it declare @c io.read("scene_depth").
        //
        // Encoding: non-linear depth24, @c .r in [0, 1], holding the NDC
        // z itself. The camera's projection comes from
        // core::math::perspective, whose clip-space depth spans [0, w]
        // (NDC z in [0, 1]), and the viewport depth range is [0, 1], so a
        // clip-space reprojection takes the sampled value unchanged.
        // shaders/include/depth_utils.glsl ships the GLSL to linearise it:
        // linearize_depth(d, near, far) =
        // near * far / (far - d * (far - near)), the positive view-space
        // distance in [near, far]. The scene pass (or, on frames it runs,
        // the depth pre-pass ahead of it) clears it to 1.0, so untouched
        // background texels linearize to the far plane.
        //
        // Synchronisation is the backend's concern: the Vulkan
        // render-pass cache rests an off-screen depth attachment in
        // SHADER_READ_ONLY_OPTIMAL between render passes (a pass that
        // loads it, the skybox, resumes from and returns to that layout).
        gpu::texture scene_depth_texture{};

        // The HDR image the post chain after motion blur works on: the
        // output of @ref motion_blur_pass when it draws this frame, else
        // @ref scene_color_target / @ref scene_color_texture themselves.
        // @ref bloom_pass thresholds this texture and composites its glow
        // back into this target, @ref auto_exposure_pass meters it and
        // @ref tonemap_pass maps it, so a disabled motion blur costs its
        // consumers nothing: they simply read the previous stage. Decided
        // by @ref renderer::render before any pass records, so every pass
        // sees the same choice; in the declared pass I/O both are the
        // logical "scene_color" resource. Passes before motion blur (the
        // scene, skybox and volumetric fog) keep using the scene-colour
        // pair.
        gpu::render_target hdr_color_target{};
        gpu::texture hdr_color_texture{};

        // Off-screen LDR colour target the tonemap pass resolves into.
        // The swapchain is not sampleable as a shader input, so the
        // final post effect (FXAA) reads its tonemapped source from this
        // intermediate rgba8 target and writes the result to
        // @ref swapchain_target. Also owned by @ref renderer.
        gpu::render_target ldr_color_target{};
        gpu::texture ldr_color_texture{};

        // Per-pixel motion vectors the velocity pass writes (signed UV
        // displacement in xy), or an invalid handle on a degenerate
        // drawable. The velocity pass only draws while a consumer needs it
        // (temporal AA or motion blur), so the contents are stale while
        // neither runs. The pass owns the target; @ref renderer publishes
        // the handle here every frame so the TAA resolve and motion blur
        // can sample it without holding a pointer to its producer, and so
        // a resize that recreates the target is picked up through the same
        // handle comparison as @ref scene_depth_texture.
        gpu::texture velocity_texture{};

        // The TAA resolve of this frame, or an invalid handle while
        // temporal AA is off. The final anti-aliasing pass (FXAA) samples
        // this when valid and @ref ldr_color_texture otherwise, so the
        // swapchain always receives a single anti-aliased image. Published
        // by @ref renderer from the pass that owns the target.
        gpu::texture taa_resolve_texture{};

        // The 1x1 eye-adaptation result @ref tonemap_pass takes its
        // exposure from (rgba16f: r = the adapted brightness in EV100,
        // g = log2 of the exposure that maps it to middle grey, the
        // compensation included), or an invalid handle while auto
        // exposure is off or has nothing metered yet — tonemap then uses
        // @ref post_settings::exposure. Owned by @ref auto_exposure_pass,
        // which writes it this frame before tonemap reads it (or, on a
        // no-camera frame, keeps the last adapted value); published by
        // @ref renderer under the same rule the pass follows.
        gpu::texture exposure_texture{};

        // The colour-grading lookup table (the strip LUT described on
        // @ref color_grading_settings) @ref tonemap_pass applies after the
        // gamma encode, or an invalid handle while grading is off (no
        // path, a table that failed to load, or a zero intensity). Loaded
        // and owned by @ref renderer through the asset cache.
        gpu::texture grading_lut_texture{};

        // Scene-wide atmospheric fog, copied from @ref renderer::set_fog
        // each frame. The scene pass packs it into the @ref view_globals
        // block so the lit materials can blend toward it by camera
        // distance. Defaults to @ref fog_mode::none (no fog).
        fog_settings fog{};

        // Whether the depth pre-pass is enabled, copied from
        // @ref renderer::set_depth_prepass each frame (seeded at init from
        // @c rendering_engine::graphics_settings::depth_prepass). @ref depth_prepass
        // records nothing while it is off or no camera is active, and the
        // scene pass then clears the scene depth itself.
        bool depth_prepass{false};

        // Runtime-tunable post-processing chain parameters, copied from
        // @ref renderer::set_post_settings each frame. @ref bloom_pass,
        // @ref taa_pass and @ref fxaa_pass read the fields they own here in
        // @c prepare and rewrite their own UBO only when a value differs
        // from what they last uploaded (@ref volumetric_fog_pass,
        // @ref motion_blur_pass and @ref auto_exposure_pass rewrite their
        // params every frame they draw, since they carry the camera, the
        // noise frame or the frame delta too); @ref tonemap_pass reads only
        // the grading intensity here — its exposure and operator are
        // applied immediately by @ref renderer::set_post_settings through
        // its own live-tunable setters. See @ref post_settings for why
        // scene-wide fog is not part of it.
        post_settings post{};

        // The passes whose per-frame output a later pass consumes,
        // published by the renderer every frame from the pass list it owns
        // (null for a pass that is absent), so no pass holds a pointer to
        // another. A consumer reads its producer here in its own
        // @ref pass::prepare, after the producer's has run (the list
        // prepares in order): the scene pass takes the shadow maps it
        // binds and the fitted matrices and culling tallies it uploads from
        // the three shadow passes; the depth pre-pass announces itself to
        // the scene pass (@ref scene_pass::expect_depth_prepass) and later
        // records the scene pass's shared draw list
        // (@ref scene_pass::record_depth_prepass); the volumetric fog binds
        // the scene pass's per-frame group and the debug pass its
        // unjittered overlay twin.
        scene_pass* scene{nullptr};
        const shadow_pass* directional_shadow{nullptr};
        const point_shadow_pass* point_shadow{nullptr};
        const spot_shadow_pass* spot_shadow{nullptr};
    };

    /**
     * @brief One step in the per-frame render sequence.
     *
     * A frame walks the renderer's ordered @ref pass_list twice in
     * @ref renderer::render: first every pass's @ref prepare, in order,
     * then every pass's @ref record, in the same order. @ref prepare is
     * where a pass computes and stores whatever the frame needs — its
     * per-frame matrices, culled and sorted draw lists, uniform-buffer
     * rewrites, bind-group rebuilds, the pipelines it will bind — and
     * where it reads what earlier passes prepared (through
     * @ref frame_context); @ref record only encodes commands from that
     * finished state and mutates nothing, so a pass may hand chunks of its
     * recording to worker threads (the scene pass does, see
     * @c render_pass_descriptor::parallel) and a pass never observes
     * another one half-way through its per-frame update. Adding a new pass
     * (post-process, shadow, depth pre-pass, debug overlay) is a
     * registration call at startup, not an edit to the engine's render
     * loop.
     *
     * A pass that owns GPU resources takes the @ref gpu::device they live
     * on as its first constructor argument, handed in by the renderer that
     * builds the pass list; the device outlives the pass. Everything that
     * changes from frame to frame reaches it through @ref frame_context.
     *
     * Names follow the industry-standard "pass" terminology even
     * though @ref gpu::render_pass_encoder shares the word; the two
     * live in different namespaces and the existing engine code
     * already uses @c pass as a local for the encoder.
     */
    struct pass
    {
        virtual ~pass() = default;

        /**
         * @brief Builds this pass's state for the frame.
         *
         * Called once per frame in registration order, on the main
         * thread, inside the device's frame bracket (host writes land in
         * this frame's slot) and before any pass records. Everything
         * @ref record needs is decided and stored here; a pass with no
         * per-frame state keeps the default no-op.
         */
        virtual void prepare(const frame_context& ctx)
        {
            (void)ctx;
        }

        /**
         * @brief Records this pass's draws on @p encoder.
         *
         * Called once per frame in registration order, after every
         * pass's @ref prepare. Implementations open their own
         * @ref gpu::render_pass_encoder via @c encoder.begin_render_pass
         * and close it before returning, encoding only from the state
         * @ref prepare left: no uploads, no resource builds, no counters.
         */
        virtual void record(gpu::command_encoder& encoder, const frame_context& ctx) = 0;

        /**
         * @brief Stable identifier used in pass-list diagnostics, debug
         *        groups and the GPU profiler.
         *
         * Defaults to a generic name; passes override it so dependency
         * warnings name the offending stage.
         */
        virtual const char* name() const
        {
            return "pass";
        }

        /**
         * @brief Declares the logical resources this pass reads and writes.
         *
         * Called once when the renderer validates its @ref pass_list, which
         * checks that every read is produced before it is consumed. Defaults
         * to declaring nothing — such a pass is recorded in place but
         * invisible to the dependency check. Passes name resources with the
         * stable strings the engine imports (e.g. "scene_color",
         * "swapchain").
         */
        virtual void declare_io(pass_io_builder& io) const
        {
            (void)io;
        }

        /**
         * @brief Notifies the pass that the drawable changed size.
         *
         * Called by @ref renderer::on_resize with the new pixel size,
         * outside any frame (no command encoder is recording), after the
         * renderer has recreated its scene-colour and LDR targets at that
         * size and before the next @ref record. Never called with a zero
         * dimension. Passes that own full-resolution targets recreate
         * them here (creating the new target before destroying the old
         * one so consumers see a different handle); passes that bake a
         * size-dependent UBO note the new size and rewrite the buffer at
         * their next @ref prepare, inside the frame bracket, since the
         * previous frame may still be reading it on a deferred-execution
         * backend until @c begin_frame waits. Passes that sample a texture
         * owned by the renderer or by another pass do not re-plumb here:
         * they compare the handle in @ref frame_context against the one
         * their bind group was built with on every @ref prepare and
         * rebuild on change, so any recreation reaches them on the next
         * frame. Defaults to a no-op for passes whose resources do not
         * follow the drawable (shadow maps, debug).
         */
        virtual void resize(uint32_t width, uint32_t height)
        {
            (void)width;
            (void)height;
        }
    };
} // namespace rendering_engine
