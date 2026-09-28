// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file material_library.hpp
 * @brief The renderer's material templates — the built-in types and any a
 *        game declares — and the instances made from them.
 */

#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    struct environment_probe;
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
     * @brief Registers the material templates and makes their instances.
     *
     * Owned by the @ref renderer, which brings it up in @ref init once the
     * passes exist (the templates are built against the per-frame layouts
     * those passes own) and takes it down in @ref quit after the passes and
     * before the device. Every template is built from a
     * @ref material_template_descriptor and registered by name through
     * @ref create_template — the eight built-in types in @ref init
     * (@c "basic", @c "instanced", @c "phong", @c "standard", @c "points",
     * @c "line", @c "grid", @c "ui"), and any type a game declares, with its
     * own shaders (asset shaders under the content directory's
     * @c shaders/), vertex format, parameter block, texture slots,
     * keywords and default render state, the same way: its variant cache
     * and keywords then work as the built-ins' do. @ref create_material
     * makes a new instance of any template, with parameters of its own:
     * the built-in type's for a built-in template, the generic
     * @ref material over the declared parameters otherwise.
     *
     * One shared instance of each built-in type is kept too, for the
     * engine's own defaults (the debug-line gizmos, the scene files'
     * built-in material references); code that sets parameters makes an
     * instance of its own instead of changing a shared one. The standard
     * template is also kept here, so @ref create_standard_material hands
     * every extra PBR instance the same one and @ref set_environment
     * reaches every live standard instance through the template's instance
     * registry. Main-thread only, like the renderer.
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
         * @brief Releases every built-in instance, then every registered
         *        template. Call before the device is torn down, once every
         *        instance made through @ref create_material is gone.
         */
        void quit();

        /**
         * @brief The per-frame layout a template whose descriptor names
         *        @p frame reserves slot 0 for: the scene pass's for
         *        @ref material_frame::scene, the UI pass's for
         *        @ref material_frame::ui, an invalid handle for
         *        @ref material_frame::none. Valid between @ref init and
         *        @ref quit.
         */
        gpu::bind_group_layout frame_layout(material_frame frame) const;

        /**
         * @brief Builds a template from @p descriptor and registers it
         *        under the descriptor's name.
         *
         * Derives what the descriptor leaves to derivation (see
         * @ref material_template_descriptor) and hands the template the
         * per-frame layout its @c frame names (@ref frame_layout). No
         * shader is compiled until an instance first needs a variant; the
         * shaders resolve through the shader library, so an asset shader
         * under the content directory's @c shaders/ is found in every build
         * type. Returns null, logging why, when the descriptor is invalid
         * (@ref material_template::validate) or a template of that name is
         * registered already. The template lives until @ref quit; a handle
         * a caller keeps must go before then. Valid between @ref init and
         * @ref quit.
         */
        std::shared_ptr<material_template> create_template(material_template_descriptor descriptor);

        /** @brief The registered template named @p name, or null. */
        std::shared_ptr<material_template> find_template(std::string_view name) const;

        /**
         * @brief A new instance of @p tmpl, owned by the caller, with
         *        parameters of its own: what the template's
         *        @c instance_factory makes (a built-in type's instance), or
         *        the generic @ref material over the template's declared
         *        parameters and textures, starting at its default render
         *        state. Null for a null template. The instance must not
         *        outlive the library.
         */
        std::unique_ptr<material> create_material(const std::shared_ptr<material_template>& tmpl) const;

        /**
         * @brief A new instance of the template registered as @p name, as
         *        @p M (e.g. @c create_material<phong_material>("phong")); null,
         *        logged, when no such template is registered or its
         *        instances are not @p M.
         */
        template<typename M = material>
        std::unique_ptr<M> create_material(std::string_view name) const
        {
            std::unique_ptr<material> made = create_material(find_template(name));
            auto* typed = dynamic_cast<M*>(made.get());
            if (typed == nullptr)
            {
                report_create_failure(name, made != nullptr);
                return nullptr;
            }
            made.release();
            return std::unique_ptr<M>{typed};
        }

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
         *        @ref create_standard_material or @ref create_material,
         *        whenever it was created — at @p environment, or back to
         *        flat ambient for @c nullptr; a standard instance
         *        @ref create_material makes later starts with it.
         */
        void set_environment(const environment_probe* environment);

        /**
         * @brief Lets every instance of a registered template that samples
         *        shared texture assets (the standard materials' maps, a
         *        game template's declared texture slots) rebuild its bind
         *        group when an asset's texture was replaced (an
         *        asynchronous load resolved, or a debug hot reload swapped
         *        it). The renderer calls it once per frame, with the frame
         *        open and before any pass records.
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
         * the @ref debug_draw::helper family.
         */
        line_material& get_debug_line_material();

        /**
         * @brief Built-in analytic infinite-grid material (the CAD-style
         *        ground grid) at the default fade distance.
         *
         * Shares the scene per-frame layout (camera at slot 0). The
         * @ref debug_draw::infinite_grid renderable builds its own material
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
         * template of its own (@ref grid_material::describe), left out of
         * the registry and kept alive by the material alone; repeat
         * compiles of a distance are served from the SPIR-V cache. Valid
         * between @ref init and @ref quit; the returned material must not
         * outlive the library.
         */
        std::unique_ptr<grid_material> create_grid_material(float fade_distance);

        /** @brief Built-in 2D overlay material. */
        ui_material& get_ui_material();

    private:
        // A template built from @p descriptor over the layout its frame
        // names, registered or not.
        std::shared_ptr<material_template> build_template(material_template_descriptor descriptor) const;

        // Logs why create_material<M>(name) made nothing.
        static void report_create_failure(std::string_view name, bool wrong_type);

        gpu::device* m_device{nullptr};
        gpu::bind_group_layout m_scene_frame_layout{};
        gpu::bind_group_layout m_ui_frame_layout{};

        // The environment set_environment last applied, which a standard
        // instance made through create_material starts with.
        const environment_probe* m_environment{nullptr};

        // Every registered template, in registration order. Declared
        // before the built-in instances so it is released after them;
        // @ref quit releases it last, explicitly.
        std::vector<std::shared_ptr<material_template>> m_templates;

        // The standard material's shared template: shaders, layouts and
        // the pipeline-variant cache every @ref standard_material
        // instance draws through, also registered in m_templates.
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
        // debug_draw::infinite_grid builds its own through
        // create_grid_material, on a template made like this one's.
        std::unique_ptr<grid_material> m_grid_material;
        std::unique_ptr<ui_material> m_ui_material;
    };
} // namespace rendering_engine
