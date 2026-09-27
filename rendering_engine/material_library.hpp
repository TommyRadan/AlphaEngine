// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file material_library.hpp
 * @brief The renderer's built-in materials and the standard template every
 *        extra PBR instance is made from.
 */

#pragma once

#include <memory>

#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
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

    namespace gpu
    {
        struct device;
    }

    /**
     * @brief Owns the built-in material templates and their instances.
     *
     * Owned by the @ref renderer, which brings it up in @ref init once the
     * passes exist (the templates are built against the per-frame layouts
     * those passes own) and takes it down in @ref quit after the passes and
     * before the device. One template per built-in type (shaders, layouts,
     * the pipeline-variant cache) and one built-in instance of each; the
     * standard template is also kept here, so @ref create_standard_material
     * hands every extra PBR instance the same one and
     * @ref set_environment reaches every live standard instance through the
     * template's instance registry. Main-thread only, like the renderer.
     */
    struct material_library
    {
        material_library();
        // Out-of-line so the std::unique_ptr members' destructors are only
        // instantiated where the material types are complete.
        ~material_library();

        material_library(const material_library&) = delete;
        material_library& operator=(const material_library&) = delete;

        /**
         * @brief Builds every built-in template and instance on @p device.
         *
         * The 3D templates reserve slot 0 for @p scene_frame_layout (the
         * scene pass's per-frame group), the ui template for
         * @p ui_frame_layout (the UI pass's pixel-space projection).
         */
        void
        init(gpu::device& device, gpu::bind_group_layout scene_frame_layout, gpu::bind_group_layout ui_frame_layout);

        /**
         * @brief Releases every instance, then the standard template (the
         *        other templates go with their last instance). Call before
         *        the device is torn down.
         */
        void quit();

        /** @brief Built-in unlit 3D scene material. */
        basic_material& get_basic_material();

        /**
         * @brief Built-in unlit instanced material fronted by
         *        @ref instanced_mesh.
         *
         * Shares the scene per-frame layout (camera at slot 0); its
         * per-draw slot reads the per-instance transform / colour storage
         * buffer the @ref instanced_mesh renderable builds.
         */
        instanced_material& get_instanced_material();

        /** @brief Built-in Blinn-Phong lit 3D scene material. */
        phong_material& get_phong_material();

        /** @brief Built-in PBR metallic-roughness lit 3D scene material. */
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
         * different pipeline variant. When @p environment is set it is
         * applied to the new material so it picks up image-based ambient
         * immediately; the renderer passes the scene's current environment
         * (see @ref renderer::create_standard_material). The returned
         * material must not outlive the library.
         */
        std::unique_ptr<standard_material> create_standard_material(const environment_probe* environment);

        /**
         * @brief The template every @ref standard_material shares, built
         *        in @ref init over the scene pass's per-frame layout.
         *
         * Exposed so game code can construct instances directly; prefer
         * @ref create_standard_material, which also applies the current
         * environment.
         */
        const std::shared_ptr<material_template>& get_standard_material_template() const;

        /**
         * @brief Points every live @ref standard_material instance — the
         *        built-in one and each one made by
         *        @ref create_standard_material, whenever it was created — at
         *        @p environment, or back to flat ambient for @c nullptr.
         */
        void set_environment(const environment_probe* environment);

        /**
         * @brief Lets every standard material that samples shared texture
         *        assets rebuild its bind group when an asset's texture was
         *        replaced (an asynchronous load resolved, or a debug hot
         *        reload swapped it). The renderer calls it once per frame,
         *        with the frame open and before any pass records.
         */
        void refresh_texture_assets();

        /** @brief Built-in unlit point-cloud material (point topology). */
        points_material& get_points_material();

        /** @brief Built-in unlit line material (line topology). */
        line_material& get_line_material();

        /**
         * @brief Built-in line material for debug gizmos — line topology
         *        with depth testing disabled.
         *
         * Shares the scene per-frame layout (camera at slot 0) with
         * @ref get_line_material, but draws depth-less so the debug
         * helpers always read on top in the depth-less debug pass. Used by
         * the @ref editor::helper family.
         */
        line_material& get_debug_line_material();

        /**
         * @brief Built-in analytic infinite-grid material (the CAD-style
         *        ground grid) at the default fade distance.
         *
         * Shares the scene per-frame layout (camera at slot 0). The
         * @ref editor::infinite_grid renderable builds its own material
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
         * the returned material must not outlive the library.
         */
        std::unique_ptr<grid_material> create_grid_material(float fade_distance);

        /** @brief Built-in 2D overlay material. */
        ui_material& get_ui_material();

    private:
        // The standard material's shared template: shaders, layouts and
        // the pipeline-variant cache every @ref standard_material
        // instance draws through. Declared first so it is released after
        // the instances; @ref quit releases it last, explicitly.
        std::shared_ptr<material_template> m_standard_template;

        // The built-in instances, released in reverse order by @ref quit.
        // Each keeps its template alive; the scene lines and the
        // depth-disabled debug-gizmo lines are two instances of one line
        // template, bound to two pipeline variants.
        std::unique_ptr<basic_material> m_basic_material;
        std::unique_ptr<instanced_material> m_instanced_material;
        std::unique_ptr<phong_material> m_phong_material;
        std::unique_ptr<standard_material> m_standard_material;
        std::unique_ptr<points_material> m_points_material;
        std::unique_ptr<line_material> m_line_material;
        std::unique_ptr<line_material> m_debug_line_material;
        // Analytic infinite-grid material at the default fade distance.
        // editor::infinite_grid builds its own through
        // create_grid_material, on a template made like this one's.
        std::unique_ptr<grid_material> m_grid_material;
        std::unique_ptr<ui_material> m_ui_material;
    };
} // namespace rendering_engine
