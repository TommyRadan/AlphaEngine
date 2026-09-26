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

#include <memory>
#include <vector>

#include <core/subscription.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>
#include <rendering_engine/render_stats.hpp>

namespace rendering_engine
{
    struct pass;
    struct renderable;
    struct skybox_pass;
    struct velocity_pass;
    struct taa_pass;
    struct environment;
    struct basic_material;
    struct instanced_material;
    struct phong_material;
    struct standard_material;
    struct points_material;
    struct line_material;
    struct grid_material;
    struct ui_material;

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
         * (active camera + swapchain target) so they cannot
         * disagree mid-frame. The built-in scene and UI passes
         * each broadcast their matching event
         * (@ref core::render_scene / @ref core::render_ui)
         * after the registry walk so debug / gizmo callers can
         * still subscribe. The walk is bracketed by
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
         * targets and size-dependent UBOs, and sets the attached camera's
         * aspect ratio (@ref camera::set_aspect_ratio) so the projection
         * matches the new drawable. Passes that sample a texture owned by
         * the context or another pass rebind on the next frame through
         * the handle comparison they make in @c record. Shadow maps are
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
         * @brief Creates a fresh @ref standard_material bound to the scene
         *        pass's per-frame layout, owned by the caller.
         *
         * Use this when a scene needs several PBR surfaces with different
         * parameters (a material grid, distinct objects) rather than the
         * single shared @ref get_standard_material. If a scene environment
         * is set (see @ref set_environment) it is applied to the new
         * material so it picks up image-based ambient immediately. The
         * returned material must not outlive the rendering context.
         */
        std::unique_ptr<standard_material> create_standard_material();

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
         *        ground grid). Constructed in @ref init.
         *
         * Shares the scene per-frame layout (camera at slot 0) and is
         * fronted by the @ref debug::infinite_grid scene renderable.
         */
        grid_material& get_grid_material();

        /** @brief Built-in 2D overlay material. Constructed in @ref init. */
        ui_material& get_ui_material();

        /**
         * @brief This frame's scene / draw statistics (renderable count,
         *        draw calls, instances, triangles, vertices).
         *
         * Refreshed by the scene pass every @ref render; the debug overlay
         * reads it. Reflects the most recently completed scene pass.
         */
        const render_stats& get_render_stats() const;

        /**
         * @brief Sets (or clears) the scene's image-based-lighting
         *        environment plus background.
         *
         * Points the skybox pass at @p env's cube map so it draws as the
         * background, and attaches the same environment to the built-in
         * @ref standard_material so its surfaces pick up image-based
         * ambient. Pass @c nullptr to drop the skybox and revert the
         * material to flat ambient. The @ref environment is non-owning and
         * must outlive the scene (or be cleared first).
         */
        void set_environment(const environment* env);

        /**
         * @brief Sets the scene-wide atmospheric fog.
         *
         * Stored and copied into the per-view @c PerFrame UBO by the
         * scene pass each frame, so the built-in lit materials
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

        // Non-owning back-pointers to the optional temporal-AA passes
        // owned by @ref m_passes, null when temporal AA is off. Kept so
        // @ref render can publish the textures they own (motion vectors,
        // the TAA resolve) through @ref frame_context every frame; their
        // consumers compare those handles and rebind on change, which is
        // how a resize that recreates the targets reaches them.
        velocity_pass* m_velocity{nullptr};
        taa_pass* m_taa{nullptr};

        // The scene pass's per-frame bind-group layout, captured in
        // @ref init so @ref create_standard_material can build additional
        // materials against the same slot 0.
        gpu::bind_group_layout m_scene_frame_layout{};

        // This frame's draw statistics, filled by the scene pass (which
        // holds a pointer to it) and surfaced via @ref get_render_stats.
        render_stats m_render_stats{};

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
        // Analytic infinite-grid material fronted by debug::infinite_grid.
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
