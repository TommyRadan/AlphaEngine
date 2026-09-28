// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file renderer.hpp
 * @brief Top-level entry point for the rendering subsystem.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <core/math/math.hpp>
#include <core/subscription.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu_profiler.hpp>
#include <rendering_engine/material_library.hpp>
#include <rendering_engine/mesh_draws.hpp>
#include <rendering_engine/passes/pass_list.hpp>
#include <rendering_engine/passes/resource_store.hpp>
#include <rendering_engine/post_settings.hpp>
#include <rendering_engine/render_services.hpp>
#include <rendering_engine/render_stats.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/ui_draws.hpp>

namespace rendering_engine
{
    struct texture_asset;
    struct environment_probe;
    struct material_template;
    struct basic_material;
    struct instanced_material;
    struct phong_material;
    struct standard_material;
    struct points_material;
    struct line_material;
    struct grid_material;
    struct ui_material;

    namespace debug_draw
    {
        struct helper;
    } // namespace debug_draw

    namespace gpu
    {
        struct overlay_renderer;
    }

#if _DEBUG
    namespace gpu
    {
        struct shader_hot_reload;
    }
#endif

    /**
     * @brief Orchestrates the rendering subsystem: the off-screen targets,
     *        the ordered pass list, the post settings and stats, and the
     *        frame itself.
     *
     * Owned by @ref runtime::engine. It owns two parts it hands out:
     *
     * - a @ref render_world — what is drawn: the mesh, UI, light and
     *   camera proxies (and the camera arbitration), the environment probe
     *   and the fog (@ref world);
     * - a @ref material_library — the built-in material templates and
     *   instances (@ref materials).
     *
     * The material and environment calls below forward to those two, so
     * game code keeps reaching them through the renderer. The owner hands @ref init every subsystem and setting
     * the renderer reads (see @ref render_services), brings the window and
     * the GPU device up before @ref init and takes them down after
     * @ref quit. @ref init constructs the built-in passes (which own their
     * per-frame bind-group layouts), then the material library (which
     * reads those layouts when building its pipelines), and registers the
     * passes through @ref add_pass — the same call engine, game or tool
     * code uses to add a pass of its own, next to @ref remove_pass and
     * @ref set_pass_enabled; the renderer keeps no pointer to any pass.
     * @ref quit tears them down in reverse order, and the members are
     * declared so that their destruction follows the same order. All
     * methods must be called from the main thread.
     */
    struct renderer
    {
        renderer();
        // Defined out-of-line in renderer.cpp so the std::unique_ptr
        // members' destructors are only instantiated where their types
        // are complete. The header keeps them forward-declared.
        ~renderer();

        /**
         * @brief Initializes the built-in passes / materials against the live gpu device @p services
         *        names, sized to its drawable, and keeps @p services for the frames that follow (see
         *        @ref render_services for what each member is read for). Must be called once before
         *        @ref render.
         */
        void init(const render_services& services);

        /** @brief Tears the materials and passes down, ahead of the gpu device and the window. */
        void quit();

        /**
         * @brief Renders one frame.
         *
         * Validates the pass list first when a pass was added or removed since
         * the last frame. Then walks the enabled passes twice — every pass's
         * @ref pass::prepare, then every pass's @ref pass::record — giving each
         * pass the same per-frame @ref frame_context (active camera, lights,
         * draw lists, viewport, frame index, this frame's and the previous
         * frame's temporal-AA jitter, the previous frame's unjittered
         * view-projection, the settings, and the frame's @ref resource_store,
         * cleared and seeded with the swapchain and the off-screen targets
         * before any pass prepares) so they cannot disagree mid-frame. The
         * active camera is the world's arbitration result
         * (@ref render_world::active_camera: the highest-priority enabled
         * camera proxy) evaluated once here, so a camera destroyed or disabled
         * since the last frame is replaced by the runner-up without any owner
         * bookkeeping, and the enabled light proxies are gathered once here in
         * packing order. The mesh and UI proxies become the frame's draw lists
         * once here too (@ref mesh_draw_builder, @ref ui_draw_builder, which
         * also upload the instance snapshots, joint palettes and UI quads the
         * proxies carry), before any pass prepares. Everything a pass reads
         * about the world is a proxy its owner wrote before the frame; the
         * world refuses to create or destroy a proxy until the frame ends
         * (@ref render_world::begin_frame). The renderer is the only place that
         * advances the frame index, the jitter sequence and the previous-frame
         * matrix, and it drops the latter across a no-camera frame or a change
         * of arbitrated camera. Every draw a pass records comes from those
         * lists (plus, for the debug pass, the ImGui draw data built before the
         * walk); no event is broadcast while a pass is recording, so debug /
         * gizmo callers create an overlay mesh proxy rather than subscribe. The
         * walk is bracketed by @c gpu::device::begin_frame / @c end_frame: the
         * device waits for the previous frame before any pass writes its
         * per-frame buffers and presents inside @c end_frame.
         */
        void render();

        /**
         * @brief Follows a change of the drawable's pixel size.
         *
         * Called by the @ref core::window_resized listener @ref init
         * installs, right after it forwarded the size to
         * @c gpu::device::resize_swapchain. The listener runs from the
         * window's event pump inside @c engine::tick, before the frame is
         * built, so no command encoder is recording; the method asserts
         * as much and must not be called from inside @ref render.
         *
         * A zero dimension (a minimised window; the main loop skips frames
         * until it is restored) and a size equal to the current one are
         * ignored. Otherwise it recreates the HDR scene-colour target (and its
         * depth) and the LDR target at the new size — new targets first, then
         * the old ones are released, so every handle the passes compare against
         * changes — calls @ref pass::resize on every pass in order so they
         * rebuild their own full-resolution targets and size-dependent UBOs,
         * drops what the last frame published (it names released targets), and
         * reports the new aspect to the world
         * (@ref render_world::set_drawable_aspect), whose camera owners hand it
         * to their cameras (@ref camera::set_aspect_ratio) before the next
         * frame, so the projection matches the new drawable. Passes that sample
         * a texture owned by the renderer or another pass rebind on the next
         * frame through the handle comparison they make in @c prepare, and the
         * temporal-AA jitter is derived from the recorded size on every
         * @ref render, so it needs no notification. Shadow maps are fixed-size
         * by design and unaffected.
         *
         * Releasing the old targets is safe here: the device defers the
         * free until the last command buffer that referenced them has
         * retired.
         */
        void on_resize(uint32_t pixel_width, uint32_t pixel_height);

        /**
         * @brief The GPU device @ref init was handed, which the passes, the
         *        materials, the render targets and the built-in debug
         *        helpers are built on. Valid between @ref init and
         *        @ref quit.
         */
        gpu::device& device() const;

        /** @brief What the renderer draws; see @ref render_world. */
        render_world& world() noexcept
        {
            return m_world;
        }

        /** @copydoc world() */
        const render_world& world() const noexcept
        {
            return m_world;
        }

        /**
         * @brief The built-in materials, built in @ref init; see
         *        @ref material_library.
         */
        material_library& materials() noexcept
        {
            return m_materials;
        }

        /**
         * @brief Sets the overlay the debug pass records after the debug
         *        geometry every frame (@ref frame_context::overlay), or null
         *        to stop. Non-owning: the caller clears it before the
         *        overlay renderer goes. Nothing draws it without a debug
         *        pass (release builds).
         */
        void set_overlay(gpu::overlay_renderer* overlay);

        /**
         * @brief Adds @p p to the pass list at @p placement: at the end of
         *        a @ref render_stage, or right before or after a pass
         *        already in the list, named as its @ref pass::name returns
         *        it (@ref builtin_passes names the built-in ones).
         *
         * The built-in passes are registered through this call in
         * @ref init; engine, game or tool code adds its own the same way,
         * at any time between @ref init and @ref quit outside a frame. The
         * renderer owns the pass from here on and destroys it in
         * @ref remove_pass or @ref quit, before the GPU device; a pass that
         * owns GPU resources builds them on @ref device. The pass joins
         * the next frame: the list is validated (see
         * @ref pass_list::validate) and the GPU profiler resized before it
         * records. Returns the pass, or null when it is refused (logged):
         * a pass of the same name is in the list, or the placement's
         * anchor is not.
         */
        pass* add_pass(std::unique_ptr<pass> p, const pass_placement& placement);

        /**
         * @brief Removes and destroys the pass named @p name — a built-in
         *        one as much as any other; false when there is none. Not
         *        during a frame. The GPU resources the pass owned are
         *        released once the frames using them have retired.
         */
        bool remove_pass(std::string_view name);

        /**
         * @brief Enables or disables the pass named @p name from the next
         *        frame; false when there is none. A disabled pass keeps its
         *        place and follows resizes, but neither prepares nor
         *        records, so it publishes nothing and the passes after it
         *        fall back as they would without it. Not during a frame.
         */
        bool set_pass_enabled(std::string_view name, bool enabled);

        /** @brief Whether a pass named @p name is in the list and enabled. */
        bool pass_enabled(std::string_view name) const;

        /** @brief @ref material_library::get_basic_material. Valid between @ref init and @ref quit. */
        basic_material& get_basic_material();

        /** @brief @ref material_library::get_instanced_material. Valid between @ref init and @ref quit. */
        instanced_material& get_instanced_material();

        /** @brief @ref material_library::get_phong_material. Valid between @ref init and @ref quit. */
        phong_material& get_phong_material();

        /** @brief @ref material_library::get_standard_material. Valid between @ref init and @ref quit. */
        standard_material& get_standard_material();

        /**
         * @brief @ref material_library::create_standard_material with the
         *        world's current environment probe (see @ref set_environment),
         *        so the new material picks up image-based ambient
         *        immediately. The returned material must not outlive the
         *        renderer.
         */
        std::unique_ptr<standard_material> create_standard_material();

        /** @brief @ref material_library::get_standard_material_template. */
        const std::shared_ptr<material_template>& get_standard_material_template() const;

        /** @brief @ref material_library::get_points_material. Valid between @ref init and @ref quit. */
        points_material& get_points_material();

        /** @brief @ref material_library::get_line_material. Valid between @ref init and @ref quit. */
        line_material& get_line_material();

        /** @brief @ref material_library::get_debug_line_material. Valid between @ref init and @ref quit. */
        line_material& get_debug_line_material();

        /** @brief @ref material_library::get_grid_material. Valid between @ref init and @ref quit. */
        grid_material& get_grid_material();

        /**
         * @brief @ref material_library::create_grid_material. Valid between
         *        @ref init and @ref quit; the returned material must not
         *        outlive the renderer.
         */
        std::unique_ptr<grid_material> create_grid_material(float fade_distance);

        /** @brief @ref material_library::get_ui_material. Valid between @ref init and @ref quit. */
        ui_material& get_ui_material();

        /**
         * @brief Sets the runtime-tunable post-processing chain parameters.
         *
         * Stored and copied into @ref frame_context::post every
         * @ref render so each post pass can read the fields it owns
         * (rewriting its own UBO only when a value actually changed; the
         * tonemap pass takes @c exposure and @c tonemap_op from here too).
         * @c taa.enabled is read-only in practice: it reports whether the
         * TAA pass is in the list and enabled (@ref init registers it from
         * @c rendering_engine::graphics_settings::temporal_aa and the
         * drawable size), so whatever this is called with is overwritten
         * with the truth before it is stored — @ref get_post_settings always
         * reports it. A new @c grading.lut path is loaded through
         * the asset cache at the top of the next @ref render (the cache is
         * only up once the engine has finished initialising). See
         * @ref post_settings for why scene-wide fog (@ref set_fog) is not
         * part of this struct. @ref init calls this with the values
         * @c rendering_engine::post_process_settings resolved at startup.
         */
        void set_post_settings(const post_settings& settings);

        /** @brief The runtime-tunable post-processing chain parameters currently in effect. */
        const post_settings& get_post_settings() const;

        /**
         * @brief Whether the colour-grading LUT @c post_settings::grading.lut
         *        names is loaded and usable. False while the path is empty,
         *        before the next @ref render resolves a new path, and when
         *        the image failed to load or is not an N^2 x N strip (the
         *        failure is logged once per path).
         */
        bool grading_lut_loaded() const;

        /**
         * @brief Turns the depth pre-pass on or off.
         *
         * Stored and copied into @ref frame_context::depth_prepass every
         * @ref render, so the change applies from the next frame: the
         * @ref depth_prepass lays the opaque queue's depth down ahead of
         * the scene pass, which then loads it and shades each pre-passed
         * surface once (see @ref depth_prepass). Seeded in @ref init from
         * @c rendering_engine::graphics_settings::depth_prepass (off by default). The
         * pass itself is always registered, so this never rebuilds the
         * list.
         */
        void set_depth_prepass(bool enabled);

        /** @brief Whether the depth pre-pass is enabled (see @ref set_depth_prepass). */
        bool depth_prepass_enabled() const;

        /**
         * @brief This frame's scene / draw statistics (mesh proxy count,
         *        draw calls, instances, triangles, vertices).
         *
         * Refreshed by the scene pass every @ref render; the debug overlay
         * reads it. Reflects the most recently completed scene pass.
         */
        const render_stats& get_render_stats() const;

        /**
         * @brief The per-pass GPU timer: last frame's GPU time of every
         *        pass, from the device's timestamp queries.
         *
         * Disabled (empty timings, @c enabled() false) on a device
         * without timestamp support. The debug overlay's profiler panel
         * reads it.
         */
        const gpu_profiler& get_gpu_profiler() const;

        /**
         * @brief Off-screen HDR scene-colour texture the scene pass renders
         *        into: post lighting, skybox, volumetric fog and bloom,
         *        pre-tonemap (while motion blur runs, bloom lands in the
         *        pass's blurred copy instead).
         *
         * Read-only accessor for tooling (the debug overlay's render-target
         * viewer); valid between @ref init and @ref quit.
         */
        gpu::texture scene_color_texture() const;

        /**
         * @brief Depth attachment of the HDR scene-colour target; see
         *        @ref frame_resources::scene_depth for the encoding.
         *        Valid between @ref init and @ref quit.
         */
        gpu::texture scene_depth_texture() const;

        /**
         * @brief Tonemapped LDR colour texture the post chain resolves
         *        into. Valid between @ref init and @ref quit.
         */
        gpu::texture ldr_color_texture() const;

        /**
         * @brief Per-pixel motion vectors the velocity pass published for
         *        the last frame (@ref frame_resources::velocity), or an
         *        invalid handle when it published none. Only rewritten
         *        while temporal AA or motion blur consumes it.
         */
        gpu::texture velocity_texture() const;

        /**
         * @brief The last frame's temporal-AA resolve output, or an
         *        invalid handle while temporal AA is off.
         */
        gpu::texture taa_resolve_texture() const;

        /**
         * @brief Directional-light cascaded shadow map the shadow pass
         *        published for the last frame: a depth 2D-array texture,
         *        one layer per cascade, holding cleared, unused depth while
         *        no shadow-casting directional light is present (see
         *        @ref directional_shadow_data). Invalid before the first
         *        frame, after a resize or a change to the pass list until
         *        the next frame, and without a shadow pass.
         */
        gpu::texture directional_shadow_map() const;

        /**
         * @brief Spot-light shadow map the spot shadow pass published for
         *        the last frame; see @ref directional_shadow_map.
         */
        gpu::texture spot_shadow_map() const;

        /** @brief @ref render_world::environment_brdf_lut on @ref world. */
        gpu::texture environment_brdf_lut() const;

        /**
         * @brief Sets (or clears) the scene's image-based-lighting
         *        environment plus background.
         *
         * Stores @p env in the @ref world, whose cube map the skybox pass
         * draws as the background from the next frame, and attaches the same
         * environment to every live @ref standard_material instance — the
         * built-in one and each one made by @ref create_standard_material,
         * whenever it was created — so their surfaces pick up image-based
         * ambient (@ref material_library::set_environment). Pass
         * @c nullptr to drop the skybox and revert the materials to flat
         * ambient. The @ref environment_probe is non-owning and must
         * outlive the scene (or be cleared first).
         */
        void set_environment(const environment_probe* env);

        /**
         * @brief Sets the scene-wide atmospheric fog in the @ref world.
         *
         * Copied into the frame context each frame and into the per-view
         * @ref view_globals block by the scene pass, so the built-in lit
         * materials (@ref phong_material, @ref standard_material) blend
         * toward the fog colour by camera distance. Pass a
         * @ref fog_settings with @ref fog_mode::none (the default) to
         * disable fog.
         */
        void set_fog(const fog_settings& fog);

    private:
        // The members are declared in dependency order: each is destroyed
        // (and @ref quit releases it) before the ones declared above it.
        // Everything that keeps proxies in the world goes before it, the
        // passes (which read it, and whose per-frame layouts the materials
        // were built against) go before the materials, and the materials
        // before the targets. @ref quit walks
        // the same order explicitly, since the GPU device the resources are
        // freed through goes down right after it, before this object is
        // destroyed.

        // The subsystems and settings handed to @ref init, read by the
        // frames that follow; cleared at the end of @ref quit.
        render_services m_services{};

        // What is drawn (see @ref world). Owns no GPU resource; declared
        // first so every pass and helper below that points into it is gone
        // before it.
        render_world m_world;

        // Off-screen HDR target the scene pass renders into.
        // Created in @ref init at the current backbuffer size, recreated
        // by @ref on_resize and released in @ref quit. Published to the
        // passes every frame as @ref frame_resources::scene_color (and its
        // depth as @ref frame_resources::scene_depth) so the post chain can
        // sample it as input.
        gpu::render_target m_scene_color_target{};
        gpu::texture m_scene_color_texture{};

        // Off-screen LDR target the tonemap pass resolves into and the
        // FXAA pass samples. rgba8, no depth; created alongside the HDR
        // target in @ref init, recreated by @ref on_resize and released in
        // @ref quit. Published every frame as
        // @ref frame_resources::ldr_color.
        gpu::render_target m_ldr_color_target{};
        gpu::texture m_ldr_color_texture{};

        // The built-in materials (see @ref materials), built in @ref init
        // after the passes, against the per-frame layouts they own, and
        // released after them.
        material_library m_materials;

        // The colour-grading LUT loaded for @ref m_grading_lut_path (null
        // when that is empty, failed to load or is not a strip LUT), and
        // the path it was resolved for. @ref update_grading_lut reloads
        // when @c m_post_settings.grading.lut differs from the path. The
        // handle keeps the cached texture alive; released in @ref quit
        // before the device.
        std::shared_ptr<texture_asset> m_grading_lut;
        std::string m_grading_lut_path;

        // This frame's draw statistics, filled by the scene pass (which
        // holds a pointer to it, so it is declared ahead of the passes) and
        // surfaced via @ref get_render_stats.
        render_stats m_render_stats{};

        // Ordered pass list recorded once per frame in @ref render.
        // Populated through @ref add_pass: by @ref init with the built-in
        // passes, and by whatever code adds its own later. Validated
        // whenever it changed, and torn down in @ref quit before the
        // materials and the GPU device the passes reference.
        pass_list m_passes;

        // The frame's resource store, handed to every pass as
        // @ref frame_context::resources: cleared and seeded with the
        // renderer's targets at the top of every @ref render, filled by
        // the passes as they prepare, and kept until the next frame so the
        // tooling accessors (@ref velocity_texture and the others) read
        // the last frame's values. It holds plain values only, so it
        // references nothing it could outlive; @ref quit clears it with
        // the passes.
        resource_store m_resources;

        // Set by @ref add_pass and @ref remove_pass; @ref render validates
        // the list and resizes the GPU profiler before the next frame.
        bool m_pass_list_changed{false};

        // The overlay the debug pass records (see @ref set_overlay),
        // handed to it as @ref frame_context::overlay. Non-owning.
        gpu::overlay_renderer* m_overlay{nullptr};

        // Per-pass GPU timer over the pass list, brought up after the list
        // is validated in @ref init and released before the device in
        // @ref quit. Surfaced via @ref get_gpu_profiler.
        gpu_profiler m_gpu_profiler;

        // Built-in debug gizmos (ground grid + world axes) created in
        // @ref init for debug builds and toggled from the debug UI. They
        // draw through mesh proxies over the material library's line and
        // grid materials, so they go (in @ref quit, and by declaration
        // order) before both.
        // Empty in release builds, where the debug pass is dropped
        // entirely.
        std::vector<std::unique_ptr<debug_draw::helper>> m_debug_helpers;

#if _DEBUG
        // Debug-build shader hot reload over the shader library's
        // override root (see gpu/shader_hot_reload.hpp). Installed in
        // @ref init right after the device, before any pass or template
        // creates a module, so every library module registers with it;
        // polled at the top of @ref render, between frames; released at
        // the start of @ref quit. Null when overrides are off.
        std::unique_ptr<gpu::shader_hot_reload> m_shader_hot_reload;
#endif

        // The window_resized listener that keeps the swapchain extent and,
        // through @ref on_resize, the off-screen targets and passes in
        // step with the drawable. Held from @ref init to @ref quit so it
        // is dropped before the device it resizes is torn down.
        core::subscription m_window_resized_subscription;

        // Runtime-tunable post-processing chain parameters, set via
        // @ref set_post_settings and copied into
        // @ref frame_context::post each @ref render so the post passes
        // can read the fields they own. Seeded by @ref init from
        // @c rendering_engine::post_process_settings, whose defaults match each pass's
        // compiled-in values.
        post_settings m_post_settings{};

        // Whether the depth pre-pass runs, set via @ref set_depth_prepass
        // (seeded from the graphics settings in @ref init) and copied into
        // @ref frame_context::depth_prepass each @ref render.
        bool m_depth_prepass_enabled{false};

        // Pixel size the two targets above (and, through pass::resize,
        // every pass) are currently built for. @ref on_resize compares
        // against it so a resize event that repeats the live size is a
        // no-op.
        uint32_t m_target_width{0};
        uint32_t m_target_height{0};

        // Set for the duration of @ref render so @ref on_resize can assert
        // it is not recreating targets while a frame is being recorded.
        bool m_in_frame{false};

        // Frames rendered so far; published as frame_context::frame_index
        // and drives the temporal-AA jitter sequence.
        uint64_t m_frame_index{0};

        // Temporal-AA bookkeeping carried from one @ref render to the
        // next: the jitter the last frame was rasterised with, the last
        // camera frame's unjittered view-projection and the camera it
        // belonged to. The matrix is published as
        // frame_context::prev_view_projection only when the next frame is
        // drawn by that same camera; a no-camera frame clears it.
        core::math::vec2 m_prev_jitter{0.0f, 0.0f};
        core::math::mat4 m_prev_view_projection{};
        bool m_has_prev_view_projection{false};
        camera_proxy_handle m_prev_camera{};

        // The enabled light proxies of the frame being rendered, in packing
        // order; refilled by every @ref render and published as
        // frame_context::lights.
        std::vector<const light_proxy*> m_frame_lights;

        // The frame's mesh draws, rebuilt by every @ref render from the
        // world's mesh proxies and published as frame_context::scene_draws
        // and overlay_draws, and the GPU resources those draws bind;
        // released in @ref quit before the materials.
        mesh_draw_builder m_mesh_draws;

        // The frame's UI draws, rebuilt by every @ref render from the
        // world's UI proxies and published as frame_context::ui_draws, and
        // the GPU resources those draws bind; released in @ref quit before
        // the materials.
        ui_draw_builder m_ui_draws;

        // Allocates the HDR scene-colour target (+ depth) and the LDR
        // target at @p width x @p height, pointing the four target members
        // at them and recording the size. Does not release what they
        // pointed at before.
        void create_color_targets(uint32_t width, uint32_t height);

        // Releases the two targets (and their attachments) and resets the
        // members. No-op for invalid handles.
        void release_color_targets();

        // Validates the pass list, logging the order, and sizes the GPU
        // profiler to it. Called by @ref init and, after a change, by
        // @ref render ahead of the frame.
        void rebuild_pass_list();

        // Whether temporal AA runs: the TAA pass is in the list and
        // enabled. Gates the projection jitter and post_settings::taa.
        bool temporal_aa_active() const;

        // Loads the colour-grading LUT @c m_post_settings.grading.lut
        // names through the asset cache (as linear data) when it differs
        // from the one last resolved, validating the strip shape. Called
        // by @ref render ahead of the frame; a path that fails is logged
        // and not retried until the path changes.
        void update_grading_lut();
    };
} // namespace rendering_engine
