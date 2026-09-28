// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file pass.hpp
 * @brief The render-pass interface and the per-frame state every pass is
 *        handed.
 */

#pragma once

#include <cstdint>
#include <span>

#include <core/math/math.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh_draws.hpp>
#include <rendering_engine/passes/resource_store.hpp>
#include <rendering_engine/post_settings.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct overlay_renderer;
    }

    struct render_world;

    /**
     * @brief The names the built-in passes register under (@ref pass::name),
     *        which a @ref pass_placement can anchor on and
     *        @ref renderer::set_pass_enabled / @ref renderer::remove_pass
     *        take.
     */
    namespace builtin_passes
    {
        inline constexpr const char* shadow = "shadow";
        inline constexpr const char* point_shadow = "point_shadow";
        inline constexpr const char* spot_shadow = "spot_shadow";
        inline constexpr const char* depth_prepass = "depth_prepass";
        inline constexpr const char* scene = "scene";
        inline constexpr const char* skybox = "skybox";
        inline constexpr const char* velocity = "velocity";
        inline constexpr const char* volumetric_fog = "volumetric_fog";
        inline constexpr const char* motion_blur = "motion_blur";
        inline constexpr const char* bloom = "bloom";
        inline constexpr const char* auto_exposure = "auto_exposure";
        inline constexpr const char* tonemap = "tonemap";
        inline constexpr const char* taa = "taa";
        inline constexpr const char* fxaa = "fxaa";
        inline constexpr const char* ui = "ui";
        inline constexpr const char* debug = "debug";
    } // namespace builtin_passes

    /**
     * @brief The per-frame state every pass is handed.
     *
     * Captured once at the top of @ref renderer::render, so passes cannot
     * disagree about which camera is active mid-frame and do not re-run
     * the camera arbitration on every entry. It carries what describes the
     * frame and its view — the camera, the lights, the draw lists built
     * from the world's proxies, the clock and frame index, the jitter, a
     * snapshot of the settings — and nothing any pass produces: what passes
     * hand each other, and the render targets they draw into, travel
     * through @ref resources.
     */
    struct frame_context
    {
        // The proxy of the camera this frame renders with — the winner of
        // @ref world's arbitration (render_world::active_camera: the
        // highest-priority enabled camera) evaluated once per frame — or
        // nullptr when no camera is enabled. Passes that need a camera
        // read its view, projection, frustum and culling mask from here,
        // never from @ref world directly, and early-return when it is
        // null. @ref active_camera_handle names it, for passes that keep
        // state per camera across frames.
        const camera_proxy* active_camera{nullptr};
        camera_proxy_handle active_camera_handle{};

        // The enabled light proxies, in the order the lights UBO packs
        // them (render_world::enabled_lights), gathered once per frame.
        std::span<const light_proxy* const> lights{};

        // One entry per mesh proxy, in the world's proxy order, built once
        // per frame (see @ref mesh_draw_builder): the draws the scene pass,
        // the depth pre-pass (through it) and the shadow passes cull by
        // view and pass kind.
        std::span<const mesh_draw> scene_draws{};

        // One entry per overlay mesh proxy (@ref mesh_description::overlay),
        // in the world's proxy order, built with @ref scene_draws: the
        // debug pass draws them on top of everything, never culled.
        std::span<const mesh_draw> overlay_draws{};

        // The UI proxies' draws, one per quad group, in paint order, built
        // once per frame (see @ref ui_draw_builder): what the UI pass
        // sorts and draws.
        std::span<const draw_item> ui_draws{};

        // The tool overlay (the editor's Dear ImGui frame) the debug pass
        // records after the overlay draws, or null for none; set through
        // @ref renderer::set_overlay.
        gpu::overlay_renderer* overlay{nullptr};

        // What this frame draws: the mesh, UI, light and camera proxies and
        // the environment probe / fog. Passes reach it only through here,
        // never through a global, so more than one render_world can exist in
        // a process. Never null once the renderer is up.
        const render_world* world{nullptr};

        // The frame's resource store: the renderer's targets (the
        // swapchain, the HDR scene colour and its depth, the LDR target,
        // the grading table) and whatever the passes before this one
        // published — every connection between passes (see
        // resource_store.hpp; the keys are in frame_resources.hpp). Never
        // null once the renderer is up. A pass looks up what it reads in
        // @ref pass::prepare and publishes what it produces there.
        resource_store* resources{nullptr};

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
        // @ref renderer::set_post_settings each frame. Each post pass reads
        // the fields it owns here in @c prepare and rewrites its own UBO
        // only when a value differs from what it last uploaded
        // (@ref volumetric_fog_pass, @ref motion_blur_pass and
        // @ref auto_exposure_pass rewrite their params every frame they
        // draw, since they carry the camera, the noise frame or the frame
        // delta too). @c taa.enabled reports whether the TAA pass is in the
        // list and enabled. See @ref post_settings for why scene-wide fog
        // is not part of it.
        post_settings post{};
    };

    /**
     * @brief One step in the per-frame render sequence.
     *
     * The renderer keeps its passes in an ordered @ref pass_list. Every
     * pass gets there through @ref renderer::add_pass — the built-in ones,
     * which @ref renderer::init registers, as much as engine, game or tool
     * code adding its own at any time outside a frame — placed at the end
     * of a @ref render_stage or right before or after a pass already in
     * the list (@ref pass_placement), never at an index. A pass can be
     * disabled and enabled again (@ref renderer::set_pass_enabled), or
     * removed (@ref renderer::remove_pass), without touching the others.
     *
     * A frame walks the enabled passes twice in @ref renderer::render:
     * first every pass's @ref prepare, in order, then every pass's
     * @ref record, in the same order. @ref prepare is where a pass computes
     * and stores whatever the frame needs — its per-frame matrices, culled
     * and sorted draw lists, uniform-buffer rewrites, bind-group rebuilds,
     * the pipelines it will bind — where it looks up in the frame's
     * @ref resource_store (@ref frame_context::resources) what the passes
     * before it produced, and where it publishes what it produces for the
     * passes after it; @ref record only encodes commands from that
     * finished state and mutates nothing, so a pass may hand chunks of its
     * recording to worker threads (the scene pass does, see
     * @c render_pass_descriptor::parallel) and a pass never observes
     * another one half-way through its per-frame update. No pass holds a
     * pointer to another: the store is the only connection, and
     * @ref declare_io names, by the same keys, what a pass reads and
     * writes there, which the list validates.
     *
     * A pass that owns GPU resources takes the @ref gpu::device they live
     * on as its first constructor argument; the device outlives the pass
     * (the renderer destroys every pass in @ref renderer::quit, before the
     * device goes). Everything that changes from frame to frame reaches it
     * through @ref frame_context.
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
         * Called once per frame in list order, on the main thread, inside
         * the device's frame bracket (host writes land in this frame's
         * slot) and before any pass records; not called while the pass is
         * disabled. Everything @ref record needs is decided and stored
         * here: the resources the pass reads are looked up here, as the
         * passes before it left them (a later pass may republish a name,
         * so a value looked up later can differ), and the ones it produces
         * are published here. A pass with no per-frame state keeps the
         * default no-op.
         */
        virtual void prepare(const frame_context& ctx)
        {
            (void)ctx;
        }

        /**
         * @brief Records this pass's draws on @p encoder.
         *
         * Called once per frame in list order, after every pass's
         * @ref prepare; not called while the pass is disabled.
         * Implementations open their own @ref gpu::render_pass_encoder via
         * @c encoder.begin_render_pass and close it before returning,
         * encoding only from the state @ref prepare left: no uploads, no
         * resource builds, no counters, and no publishing. A lookup here
         * sees each resource as the last pass left it, so it is only for
         * a value a later pass prepares (declared
         * @ref pass_io_builder::read_unordered).
         */
        virtual void record(gpu::command_encoder& encoder, const frame_context& ctx) = 0;

        /**
         * @brief The pass's name: unique within the list, what a
         *        @ref pass_placement anchors on, and what list diagnostics,
         *        debug groups and the GPU profiler show.
         */
        virtual const char* name() const = 0;

        /**
         * @brief Declares the resources this pass reads and writes, by the
         *        keys it looks them up and publishes them under.
         *
         * Called once, when the pass is added to the list; the list checks
         * the order of the declarations whenever it changes (see
         * @ref pass_list::validate), and debug builds report any publish or
         * lookup the declarations do not cover. Defaults to declaring
         * nothing — such a pass is recorded in place but invisible to the
         * dependency check.
         */
        virtual void declare_io(pass_io_builder& io) const
        {
            (void)io;
        }

        /**
         * @brief Notifies the pass that the drawable changed size.
         *
         * Called by @ref renderer::on_resize with the new pixel size on
         * every pass in the list, enabled or not, outside any frame (no
         * command encoder is recording), after the renderer has recreated
         * its scene-colour and LDR targets at that size and before the next
         * @ref record. Never called with a zero dimension. Passes that own
         * full-resolution targets recreate them here (creating the new
         * target before destroying the old one, so consumers see a
         * different handle); passes that bake a size-dependent UBO note
         * the new size and rewrite the buffer at their next @ref prepare,
         * inside the frame bracket, since the previous frame may still be
         * reading it on a deferred-execution backend until @c begin_frame
         * waits. Passes that sample a texture another pass or the renderer
         * owns do not re-plumb here: they compare the handle they look up
         * in the store against the one their bind group was built with on
         * every @ref prepare and rebuild on change, so any recreation
         * reaches them on the next frame. Defaults to a no-op for passes
         * whose resources do not follow the drawable (shadow maps, debug).
         */
        virtual void resize(uint32_t width, uint32_t height)
        {
            (void)width;
            (void)height;
        }
    };
} // namespace rendering_engine
