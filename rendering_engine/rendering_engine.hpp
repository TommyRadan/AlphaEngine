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
 * @file rendering_engine.hpp
 * @brief Top-level entry point for the rendering subsystem.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <core/math/math.hpp>
#include <core/subscription.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu_profiler.hpp>
#include <rendering_engine/post_settings.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>
#include <rendering_engine/render_stats.hpp>

namespace rendering_engine
{
    struct camera;
    struct pass;
    struct renderable;
    struct skybox_pass;
    struct tonemap_pass;
    struct velocity_pass;
    struct taa_pass;
    struct shadow_pass;
    struct spot_shadow_pass;
    struct environment;
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

    namespace debug
    {
        struct helper;
    }

    /**
     * @brief Orchestrates the rendering subsystem (window, GL context, materials, passes).
     *
     * Owned by @ref runtime::engine. @ref init brings up the window
     * and OpenGL context, constructs the built-in passes (which own
     * their per-frame bind-group layouts) and the built-in materials
     * (which read those layouts when building their pipelines);
     * @ref quit tears them down in reverse order. All methods must be
     * called from the main thread that owns the GL context.
     */
    struct context
    {
        context();
        // Defined out-of-line in rendering_engine.cpp so the
        // std::vector<std::unique_ptr<pass>> destructor is only
        // instantiated where @ref pass is a complete type. The
        // header keeps @c pass forward-declared.
        ~context();

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
         * Walks the ordered pass list registered in @ref init,
         * giving each pass the same per-frame @ref frame_context
         * (active camera, swapchain and off-screen targets, viewport,
         * frame index, this frame's and the previous frame's
         * temporal-AA jitter, and the previous frame's unjittered
         * view-projection) so they cannot disagree mid-frame. The
         * active camera is the camera registry's arbitration result
         * (@ref active_camera: the highest-priority attached, enabled
         * camera) evaluated once here, so a camera destroyed or
         * disabled since the last frame is replaced by the runner-up
         * without any owner bookkeeping. The context is the only
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
         * the camera registry (@ref set_drawable_aspect), which forwards
         * it to every attached camera's @ref camera::set_aspect_ratio and
         * to cameras attached later, so the projection matches the new
         * drawable. Passes that sample a texture owned by
         * the context or another pass rebind on the next frame through
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

        /**
         * @brief Adds @p r to the scene-pass registry.
         *
         * The pointer is non-owning; callers must @ref unregister_scene_renderable
         * before destroying the renderable. Registration order is
         * preserved and is the dispatch order during the scene pass.
         */
        void register_scene_renderable(renderable* r);

        /** @brief Removes @p r from the scene-pass registry; no-op if absent. */
        void unregister_scene_renderable(renderable* r);

        /** @brief Adds @p r to the UI-pass registry; same ownership rules as the scene variant. */
        void register_ui_renderable(renderable* r);

        /** @brief Removes @p r from the UI-pass registry; no-op if absent. */
        void unregister_ui_renderable(renderable* r);

        /**
         * @brief Adds @p r to the debug-pass registry.
         *
         * Renderables registered here are drawn after the UI pass in
         * debug builds; release builds drop the debug pass from the
         * pass list entirely so registrations are inert. Same
         * ownership rules as the scene variant.
         */
        void register_debug_renderable(renderable* r);

        /** @brief Removes @p r from the debug-pass registry; no-op if absent. */
        void unregister_debug_renderable(renderable* r);

        /** @brief Built-in unlit 3D scene material. Constructed in @ref init. */
        basic_material& get_basic_material();

        /**
         * @brief Built-in unlit instanced material fronted by
         *        @ref instanced_mesh. Constructed in @ref init.
         *
         * Shares the scene per-frame layout (camera at slot 0); its
         * per-draw slot reads the per-instance transform / colour storage
         * buffer the @ref instanced_mesh renderable builds.
         */
        instanced_material& get_instanced_material();

        /** @brief Built-in Blinn-Phong lit 3D scene material. Constructed in @ref init. */
        phong_material& get_phong_material();

        /** @brief Built-in PBR metallic-roughness lit 3D scene material. Constructed in @ref init. */
        standard_material& get_standard_material();

        /**
         * @brief Creates a fresh @ref standard_material instance of the
         *        shared standard template, owned by the caller.
         *
         * Use this when a scene needs several PBR surfaces with different
         * parameters (a material grid, distinct objects) rather than the
         * single shared @ref get_standard_material. Every instance shares
         * the one template (see @ref get_standard_material_template):
         * N materials cost one set of shaders and layouts, and only
         * instances whose keywords or base params differ draw through a
         * different pipeline variant. If a scene environment is set (see
         * @ref set_environment) it is applied to the new material so it
         * picks up image-based ambient immediately. The returned material
         * must not outlive the rendering context.
         */
        std::unique_ptr<standard_material> create_standard_material();

        /**
         * @brief The template every @ref standard_material shares, built
         *        in @ref init over the scene pass's per-frame layout.
         *
         * Exposed so game code can construct instances directly; prefer
         * @ref create_standard_material, which also applies the current
         * environment.
         */
        const std::shared_ptr<material_template>& get_standard_material_template() const;

        /** @brief Built-in unlit point-cloud material (point topology). Constructed in @ref init. */
        points_material& get_points_material();

        /** @brief Built-in unlit line material (line topology). Constructed in @ref init. */
        line_material& get_line_material();

        /**
         * @brief Built-in line material for debug gizmos — line topology
         *        with depth testing disabled.
         *
         * Shares the scene per-frame layout (camera at slot 0) with
         * @ref get_line_material, but draws depth-less so the debug
         * helpers always read on top in the
         * depth-less debug pass. Constructed in @ref init; used by the
         * @ref debug::helper family.
         */
        line_material& get_debug_line_material();

        /**
         * @brief Built-in analytic infinite-grid material (the CAD-style
         *        ground grid) at the default fade distance. Constructed
         *        in @ref init.
         *
         * Shares the scene per-frame layout (camera at slot 0). The
         * @ref debug::infinite_grid renderable builds its own material
         * through @ref create_grid_material so its fade distance is
         * honoured; this shared one serves callers that want the default.
         */
        grid_material& get_grid_material();

        /**
         * @brief Creates a @ref grid_material bound to the scene pass's
         *        per-frame layout that fades out @p fade_distance world
         *        units from the camera, owned by the caller.
         *
         * The fade distance is baked into the grid template's fragment
         * shader as a define, so the material comes with a grid
         * template of its own (@ref grid_material::create_template),
         * which it keeps alive; repeat compiles of a distance are served
         * from the SPIR-V cache. Valid between @ref init and @ref quit;
         * the returned material must not outlive the rendering context.
         */
        std::unique_ptr<grid_material> create_grid_material(float fade_distance);

        /** @brief Built-in 2D overlay material. Constructed in @ref init. */
        ui_material& get_ui_material();

        /**
         * @brief The per-frame allocator the 3D renderables write their
         *        PerDraw block into (see @ref per_draw_ring). Created in
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
         * reports the truth. See @ref post_settings for why scene-wide fog
         * (@ref set_fog) is not part of this struct.
         */
        void set_post_settings(const post_settings& settings);

        /** @brief The runtime-tunable post-processing chain parameters currently in effect. */
        const post_settings& get_post_settings() const;

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
         *        frame-graph pass, from the device's timestamp queries.
         *
         * Disabled (empty timings, @c enabled() false) on a device
         * without timestamp support. The debug overlay's profiler panel
         * reads it.
         */
        const gpu_profiler& get_gpu_profiler() const;

        /**
         * @brief Off-screen HDR scene-colour texture the scene pass renders
         *        into: post lighting, skybox and bloom, pre-tonemap.
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
         *        invalid handle while temporal AA is off.
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

        /**
         * @brief The active environment's BRDF look-up table (a 2D
         *        scale/bias table), or an invalid handle when no
         *        environment is set (see @ref set_environment).
         */
        gpu::texture environment_brdf_lut() const;

        /**
         * @brief Sets (or clears) the scene's image-based-lighting
         *        environment plus background.
         *
         * Points the skybox pass at @p env's cube map so it draws as the
         * background, and attaches the same environment to every live
         * @ref standard_material instance — the built-in one and each
         * one made by @ref create_standard_material, whenever it was
         * created — so their surfaces pick up image-based ambient. Pass
         * @c nullptr to drop the skybox and revert the materials to flat
         * ambient. The @ref environment is non-owning and must outlive
         * the scene (or be cleared first).
         */
        void set_environment(const environment* env);

        /**
         * @brief Sets the scene-wide atmospheric fog.
         *
         * Stored and copied into the per-view @ref view_globals block by
         * the scene pass each frame, so the built-in lit materials
         * (@ref phong_material, @ref standard_material) blend toward the
         * fog colour by camera distance. Pass a @ref fog_settings with
         * @ref fog_mode::none (the default) to disable fog.
         */
        void set_fog(const fog_settings& fog);

    private:
        std::vector<renderable*> m_scene_renderables;
        std::vector<renderable*> m_ui_renderables;
        std::vector<renderable*> m_debug_renderables;

        // Built-in debug gizmos (ground grid + world axes) created in
        // @ref init for debug builds and toggled from the debug UI. They
        // auto-register into @ref m_debug_renderables on construction, so
        // this list owns their lifetime and must be cleared in @ref quit
        // before the line material and GPU device they reference. Empty in
        // release builds, where the debug pass is dropped entirely.
        std::vector<std::unique_ptr<debug::helper>> m_debug_helpers;

        // Ordered pass list walked once per frame in @ref render.
        // Populated by @ref init with the built-in scene + UI passes
        // and torn down first in @ref quit, before the materials and
        // GPU device the passes reference.
        std::vector<std::unique_ptr<pass>> m_passes;

        // Declarative view over @ref m_passes built once in @ref init: each
        // pass declares the resources it reads/writes, the graph validates the
        // ordering, and @ref render executes through it. Execution order equals
        // the @ref m_passes order, so the graph does not change rendering.
        render_graph::frame_graph m_frame_graph;

        // Non-owning back-pointer to the skybox pass owned by
        // @ref m_passes. Kept so @ref set_environment can swap its cube
        // map after construction. Null until @ref init runs.
        skybox_pass* m_skybox{nullptr};

        // Non-owning back-pointer to the tonemap pass owned by
        // @ref m_passes, surfaced through @ref tonemap so its exposure
        // and operator can be tuned live. Null until @ref init runs.
        tonemap_pass* m_tonemap{nullptr};

        // Non-owning back-pointers to the optional temporal-AA passes
        // owned by @ref m_passes, null when temporal AA is off. Kept so
        // @ref render can publish the textures they own (motion vectors,
        // the TAA resolve) through @ref frame_context every frame; their
        // consumers compare those handles and rebind on change, which is
        // how a resize that recreates the targets reaches them.
        velocity_pass* m_velocity{nullptr};
        taa_pass* m_taa{nullptr};

        // Non-owning back-pointers to the directional and spot shadow
        // passes owned by @ref m_passes, surfaced through
        // @ref directional_shadow_map / @ref spot_shadow_map for tooling
        // (the debug overlay's render-target viewer). Null until
        // @ref init runs.
        shadow_pass* m_shadow{nullptr};
        spot_shadow_pass* m_spot_shadow{nullptr};

        // The standard material's shared template: shaders, layouts and
        // the pipeline-variant cache every @ref standard_material
        // instance draws through. Kept here so
        // @ref create_standard_material can hand new instances the same
        // one and @ref set_environment can reach every live instance.
        // The other built-in types' templates are held only by their
        // single built-in instance. Released after the materials in
        // @ref quit.
        std::shared_ptr<material_template> m_standard_template;

        // The per-draw ring (see @ref get_per_draw_ring).
        std::unique_ptr<per_draw_ring> m_per_draw_ring;

        // This frame's draw statistics, filled by the scene pass (which
        // holds a pointer to it) and surfaced via @ref get_render_stats.
        render_stats m_render_stats{};

        // Per-pass GPU timer over the frame graph, brought up after the
        // graph is compiled in @ref init and released before the device
        // in @ref quit. Surfaced via @ref get_gpu_profiler.
        gpu_profiler m_gpu_profiler;

        // The window_resized listener that keeps the swapchain extent and,
        // through @ref on_resize, the off-screen targets and passes in
        // step with the drawable. Held from @ref init to @ref quit so it
        // is dropped before the device it resizes is torn down.
        core::subscription m_window_resized_subscription;

        // The active scene environment, or null. Stored so newly created
        // materials inherit the image-based lighting. Non-owning.
        const environment* m_environment{nullptr};

        // Scene-wide atmospheric fog, set via @ref set_fog and copied
        // into the frame context each @ref render so the scene pass can
        // upload it. Defaults to @ref fog_mode::none (disabled).
        fog_settings m_fog{};

        // Runtime-tunable post-processing chain parameters, set via
        // @ref set_post_settings and copied into
        // @ref frame_context::post each @ref render so bloom, TAA and
        // FXAA can read the fields they own. Defaults match what each
        // pass already baked in before this existed, so a context that
        // never calls @ref set_post_settings renders identically.
        post_settings m_post_settings{};

        // Built-in materials, constructed after the passes in
        // @ref init so they can read the passes' per-frame bind-group
        // layouts. Released after the passes in @ref quit.
        std::unique_ptr<basic_material> m_basic_material;
        std::unique_ptr<instanced_material> m_instanced_material;
        std::unique_ptr<phong_material> m_phong_material;
        std::unique_ptr<standard_material> m_standard_material;
        std::unique_ptr<points_material> m_points_material;
        std::unique_ptr<line_material> m_line_material;
        // Depth-disabled line material the debug gizmos draw through.
        std::unique_ptr<line_material> m_debug_line_material;
        // Analytic infinite-grid material at the default fade distance.
        // debug::infinite_grid builds its own through
        // create_grid_material, on a template made like this one's.
        std::unique_ptr<grid_material> m_grid_material;
        std::unique_ptr<ui_material> m_ui_material;

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
        // target at @p width x @p height, pointing the four members above
        // at them and recording the size. Does not release what they
        // pointed at before.
        void create_color_targets(uint32_t width, uint32_t height);

        // Releases the two targets (and their attachments) and resets the
        // members. No-op for invalid handles.
        void release_color_targets();
    };
} // namespace rendering_engine
