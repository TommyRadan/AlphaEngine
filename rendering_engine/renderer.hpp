/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

/**
 * @file renderer.hpp
 * @brief Top-level entry point for the rendering subsystem.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <core/math/math.hpp>
#include <core/subscription.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu_profiler.hpp>
#include <rendering_engine/material_library.hpp>
#include <rendering_engine/passes/pass_list.hpp>
#include <rendering_engine/post_settings.hpp>
#include <rendering_engine/render_stats.hpp>
#include <rendering_engine/render_world.hpp>

namespace rendering_engine
{
    struct camera;
    struct renderable;
    struct scene_pass;
    struct skybox_pass;
    struct tonemap_pass;
    struct velocity_pass;
    struct motion_blur_pass;
    struct auto_exposure_pass;
    struct taa_pass;
    struct texture_asset;
    struct shadow_pass;
    struct point_shadow_pass;
    struct spot_shadow_pass;
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
    class per_draw_ring;

    namespace editor
    {
        struct helper;
    }

#if _DEBUG
    namespace gpu
    {
        struct shader_hot_reload;
    }
#endif

    /**
     * @brief Orchestrates the rendering subsystem: the window and GPU
     *        device bring-up, the off-screen targets, the ordered pass
     *        list, the post settings and stats, and the frame itself.
     *
     * Owned by @ref runtime::engine. It owns two parts it hands out:
     *
     * - a @ref render_world — what is drawn: the renderable registries,
     *   the lights and cameras (and the camera arbitration), the
     *   environment probe and the fog (@ref world);
     * - a @ref material_library — the built-in material templates and
     *   instances (@ref materials).
     *
     * The registration, material and environment calls below forward to
     * those two, so renderables and game code keep reaching them through
     * the renderer. @ref init brings up the window and GPU device,
     * constructs the built-in passes (which own their per-frame bind-group
     * layouts) and then the material library (which reads those layouts
     * when building its pipelines); @ref quit tears them down in reverse
     * order, and the members are declared so that their destruction
     * follows the same order. All methods must be called from the main
     * thread that owns the GL context.
     */
    struct renderer
    {
        renderer();
        // Defined out-of-line in renderer.cpp so the std::unique_ptr
        // members' destructors are only instantiated where their types
        // are complete. The header keeps them forward-declared.
        ~renderer();

        /**
         * @brief Initializes the window, GL context and built-in passes / materials.
         *        Must be called once before @ref render.
         */
        void init();

        /** @brief Tears the materials, passes, GL context and window down. */
        void quit();

        /**
         * @brief Renders one frame.
         *
         * Walks the ordered pass list registered in @ref init twice —
         * every pass's @ref pass::prepare, then every pass's
         * @ref pass::record — giving each pass the same per-frame
         * @ref frame_context (active camera, swapchain and off-screen
         * targets, viewport, frame index, this frame's and the previous
         * frame's temporal-AA jitter, and the previous frame's unjittered
         * view-projection) so they cannot disagree mid-frame. The
         * active camera is the world's arbitration result
         * (@ref render_world::active_camera: the highest-priority attached,
         * enabled camera) evaluated once here, so a camera destroyed or
         * disabled since the last frame is replaced by the runner-up
         * without any owner bookkeeping. The renderer is the only
         * place that advances the frame index, the jitter sequence
         * and the previous-frame matrix, and it drops the latter
         * across a no-camera frame or a change of arbitrated camera.
         * Every draw a pass records comes from its renderable
         * registry (plus, for the debug pass, the ImGui draw data
         * built before the walk); no event is broadcast while a pass
         * is recording, so debug / gizmo callers register a
         * renderable rather than subscribe. The walk is bracketed by
         * @c gpu::device::begin_frame / @c end_frame: the Vulkan
         * backend waits for the previous frame before any pass
         * writes its per-frame buffers and presents inside
         * @c end_frame, while OpenGL presents when the caller
         * invokes @c window::swap_buffers.
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
         * ignored. Otherwise it recreates the HDR scene-colour target (and
         * its depth) and the LDR target at the new size — new targets
         * first, then the old ones are released, so every handle the
         * passes compare against changes — calls @ref pass::resize on
         * every pass in order so they rebuild their own full-resolution
         * targets and size-dependent UBOs, and reports the new aspect to
         * the world (@ref render_world::set_drawable_aspect), which forwards
         * it to every attached camera's @ref camera::set_aspect_ratio and
         * to cameras attached later, so the projection matches the new
         * drawable. Passes that sample a texture owned by
         * the renderer or another pass rebind on the next frame through
         * the handle comparison they make in @c record, and the
         * temporal-AA jitter is derived from the recorded size on every
         * @ref render, so it needs no notification. Shadow maps are
         * fixed-size by design and unaffected.
         *
         * Releasing the old targets is safe here on both backends: the
         * OpenGL device frees immediately (nothing is bound outside a
         * frame) and the Vulkan device defers the free until the last
         * command buffer that referenced them has retired.
         */
        void on_resize(uint32_t pixel_width, uint32_t pixel_height);

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

        /** @brief @ref render_world::register_scene_renderable on @ref world. */
        void register_scene_renderable(renderable* r);

        /** @brief @ref render_world::unregister_scene_renderable on @ref world. */
        void unregister_scene_renderable(renderable* r);

        /** @brief @ref render_world::register_ui_renderable on @ref world. */
        void register_ui_renderable(renderable* r);

        /** @brief @ref render_world::unregister_ui_renderable on @ref world. */
        void unregister_ui_renderable(renderable* r);

        /** @brief @ref render_world::register_debug_renderable on @ref world. */
        void register_debug_renderable(renderable* r);

        /** @brief @ref render_world::unregister_debug_renderable on @ref world. */
        void unregister_debug_renderable(renderable* r);

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
         * @brief The per-frame allocator the 3D renderables write their
         *        PerDraw block into on a device without push constants
         *        (see @ref per_draw_ring). Created in
         *        @ref init right after the device, rewound by @ref render
         *        at the top of every frame and released in @ref quit
         *        before the device.
         */
        per_draw_ring& get_per_draw_ring();

        /**
         * @brief The tonemap post pass, for live tuning of its exposure
         *        and operator (@ref tonemap_pass::set_exposure /
         *        @ref tonemap_pass::set_operator). Constructed in
         *        @ref init; valid between @ref init and @ref quit.
         */
        tonemap_pass& tonemap();

        /**
         * @brief Sets the runtime-tunable post-processing chain parameters.
         *
         * Stored and copied into @ref frame_context::post every
         * @ref render so @ref volumetric_fog_pass, @ref bloom_pass,
         * @ref taa_pass and @ref fxaa_pass can read the fields they own
         * (the latter three rewriting their own UBO only when a value
         * actually changed). @c exposure and @c tonemap_op are the
         * exception: they are forwarded immediately to
         * @ref tonemap_pass::set_exposure / @ref tonemap_pass::set_operator
         * (already live-tunable the same way), so a caller reading
         * @ref tonemap right after this call sees the new values without
         * waiting for a frame. @c taa.enabled is read-only in practice:
         * whether @ref taa_pass exists is decided once in @ref init from
         * @c core::settings::graphics.temporal_aa and the drawable size, so
         * whatever this is called with is overwritten with the pass's real
         * presence before it is stored — @ref get_post_settings always
         * reports the truth. A new @c grading.lut path is loaded through
         * the asset cache at the top of the next @ref render (the cache is
         * only up once the engine has finished initialising). See
         * @ref post_settings for why scene-wide fog (@ref set_fog) is not
         * part of this struct. @ref init calls this with the values
         * @c core::settings::post resolved at startup.
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
         * @c core::settings::graphics.depth_prepass (off by default). The
         * pass itself is always in the pass list, so this never rebuilds
         * it.
         */
        void set_depth_prepass(bool enabled);

        /** @brief Whether the depth pre-pass is enabled (see @ref set_depth_prepass). */
        bool depth_prepass_enabled() const;

        /**
         * @brief This frame's scene / draw statistics (renderable count,
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
         *        @ref frame_context::scene_depth_texture for the encoding.
         *        Valid between @ref init and @ref quit.
         */
        gpu::texture scene_depth_texture() const;

        /**
         * @brief Tonemapped LDR colour texture the post chain resolves
         *        into. Valid between @ref init and @ref quit.
         */
        gpu::texture ldr_color_texture() const;

        /**
         * @brief Per-pixel motion vectors from the velocity pass, or an
         *        invalid handle on a degenerate drawable. Only rewritten
         *        while temporal AA or motion blur consumes it.
         */
        gpu::texture velocity_texture() const;

        /**
         * @brief This frame's temporal-AA resolve output, or an invalid
         *        handle while temporal AA is off.
         */
        gpu::texture taa_resolve_texture() const;

        /**
         * @brief Directional-light cascaded shadow map: a depth 2D-array
         *        texture, one layer per cascade (see
         *        @ref shadow_pass::cascade_count). Valid between
         *        @ref init and @ref quit; holds cleared, unused depth
         *        while no shadow-casting directional light is present
         *        (see @ref shadow_pass::has_shadow).
         */
        gpu::texture directional_shadow_map() const;

        /**
         * @brief Spot-light shadow map. Valid between @ref init and
         *        @ref quit; see @ref directional_shadow_map.
         */
        gpu::texture spot_shadow_map() const;

        /** @brief @ref render_world::environment_brdf_lut on @ref world. */
        gpu::texture environment_brdf_lut() const;

        /**
         * @brief Sets (or clears) the scene's image-based-lighting
         *        environment plus background.
         *
         * Stores @p env in the @ref world, points the skybox pass at its
         * cube map so it draws as the background, and attaches the same
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
        // Everything that registers into the world goes before it, the
        // passes (which point into its registries, and whose per-frame
        // layouts the materials were built against) go before the
        // materials, and the materials before the per-draw ring and the
        // targets. @ref quit walks the same order explicitly, since the
        // GPU device the resources are freed through goes down right after
        // it, before this object is destroyed.

        // What is drawn (see @ref world). Owns no GPU resource; declared
        // first so every renderable, pass and helper below that points
        // into its registries is gone before it.
        render_world m_world;

        // Off-screen HDR target the scene pass renders into.
        // Created in @ref init at the current backbuffer size, recreated
        // by @ref on_resize and released in @ref quit. Surfaced to passes
        // via @ref frame_context::scene_color_target / @c scene_color_texture
        // so the post chain can sample it as input.
        gpu::render_target m_scene_color_target{};
        gpu::texture m_scene_color_texture{};

        // Off-screen LDR target the tonemap pass resolves into and the
        // FXAA pass samples. rgba8, no depth; created alongside the HDR
        // target in @ref init, recreated by @ref on_resize and released in
        // @ref quit. Surfaced via @ref frame_context::ldr_color_target /
        // @c ldr_color_texture.
        gpu::render_target m_ldr_color_target{};
        gpu::texture m_ldr_color_texture{};

        // The per-draw ring (see @ref get_per_draw_ring). Created in
        // @ref init right after the device; every renderable has released
        // its per-draw state (offsets only, no ring resources) by @ref quit.
        std::unique_ptr<per_draw_ring> m_per_draw_ring;

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
        // Populated by @ref init with the built-in passes in render order,
        // which validates the resources each declares, and torn down in
        // @ref quit before the materials and the GPU device the passes
        // reference.
        pass_list m_passes;

        // Non-owning back-pointer to the skybox pass owned by
        // @ref m_passes. Kept so @ref set_environment can swap its cube
        // map after construction. Null until @ref init runs.
        skybox_pass* m_skybox{nullptr};

        // Non-owning back-pointer to the tonemap pass owned by
        // @ref m_passes, surfaced through @ref tonemap so its exposure
        // and operator can be tuned live. Null until @ref init runs.
        tonemap_pass* m_tonemap{nullptr};

        // Non-owning back-pointers to the velocity pass and the optional
        // TAA pass owned by @ref m_passes (the latter null when temporal
        // AA is off). Kept so @ref render can publish the textures they own
        // (motion vectors, the TAA resolve) through @ref frame_context
        // every frame; their consumers compare those handles and rebind on
        // change, which is how a resize that recreates the targets reaches
        // them.
        velocity_pass* m_velocity{nullptr};
        taa_pass* m_taa{nullptr};

        // Non-owning back-pointers to the motion-blur and auto-exposure
        // passes owned by @ref m_passes. @ref render asks them, before any
        // pass records, whether they produce output this frame, and
        // publishes it (@ref frame_context::hdr_color_target /
        // @c hdr_color_texture, @ref frame_context::exposure_texture) for
        // the passes after them only when they do.
        motion_blur_pass* m_motion_blur{nullptr};
        auto_exposure_pass* m_auto_exposure{nullptr};

        // Non-owning back-pointers to the scene pass and the three shadow
        // passes owned by @ref m_passes. @ref render publishes them through
        // @ref frame_context (@c scene, @c directional_shadow,
        // @c point_shadow, @c spot_shadow) every frame for the passes that
        // consume their output, so no pass holds another; the directional
        // and spot maps are also surfaced through
        // @ref directional_shadow_map / @ref spot_shadow_map for tooling
        // (the debug overlay's render-target viewer). Null until
        // @ref init runs.
        scene_pass* m_scene{nullptr};
        shadow_pass* m_shadow{nullptr};
        point_shadow_pass* m_point_shadow{nullptr};
        spot_shadow_pass* m_spot_shadow{nullptr};

        // Per-pass GPU timer over the pass list, brought up after the list
        // is validated in @ref init and released before the device in
        // @ref quit. Surfaced via @ref get_gpu_profiler.
        gpu_profiler m_gpu_profiler;

        // Built-in debug gizmos (ground grid + world axes) created in
        // @ref init for debug builds and toggled from the debug UI. They
        // auto-register into the world's registries on construction and
        // draw through the material library's line and grid materials, so
        // they go (in @ref quit, and by declaration order) before both.
        // Empty in release builds, where the debug pass is dropped
        // entirely.
        std::vector<std::unique_ptr<editor::helper>> m_debug_helpers;

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
        // @c core::settings::post, whose defaults match what each pass
        // baked in before either existed.
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
        const camera* m_prev_camera{nullptr};

        // Allocates the HDR scene-colour target (+ depth) and the LDR
        // target at @p width x @p height, pointing the four target members
        // at them and recording the size. Does not release what they
        // pointed at before.
        void create_color_targets(uint32_t width, uint32_t height);

        // Releases the two targets (and their attachments) and resets the
        // members. No-op for invalid handles.
        void release_color_targets();

        // Loads the colour-grading LUT @c m_post_settings.grading.lut
        // names through the asset cache (as linear data) when it differs
        // from the one last resolved, validating the strip shape. Called
        // by @ref render ahead of the frame; a path that fails is logged
        // and not retried until the path changes.
        void update_grading_lut();
    };
} // namespace rendering_engine
