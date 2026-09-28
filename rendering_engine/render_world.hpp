// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_world.hpp
 * @brief What the renderer draws: mesh, UI, light and camera proxies, the
 *        environment probe and the scene-wide fog.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <core/dense_pool.hpp>
#include <core/math/mat4.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/ui_proxy.hpp>
#include <rendering_engine/view.hpp>

namespace rendering_engine
{
    struct environment_probe;

    namespace debug_draw
    {
        struct helper;
    }

    /**
     * @brief The renderer's view of the world.
     *
     * Owned by the @ref renderer, which reads it once per frame to fill the
     * @ref frame_context. It owns:
     *
     * - the mesh proxies (@ref mesh_proxy) the scene, depth pre-pass and
     *   shadow passes draw, and the debug overlay's, in creation order,
     *   which is the order the passes walk them in (@ref meshes);
     * - the UI proxies (@ref ui_proxy), in creation order, which is the
     *   order they paint in (@ref ui_elements);
     * - the light proxies (@ref light_proxy), and the order the enabled ones
     *   are packed into the lights UBO in (@ref enabled_lights);
     * - the camera proxies (@ref camera_proxy), the arbitration that picks
     *   the camera the swapchain renders with (@ref active_camera) and the
     *   list of views a frame renders (@ref collect_views);
     * - the debug-draw helper list the debug UI walks to toggle visibility
     *   and @ref debug_draw::update_helpers to rebuild the gizmos
     *   (@ref helpers), which no pass reads;
     * - the environment probe the skybox and the standard materials use;
     * - the scene-wide atmospheric fog.
     *
     * A proxy is a plain copy the world owns, addressed by a handle: its
     * creator (a runtime mesh, renderable, UI element, light or camera
     * component, or a debug helper) creates it, enables it, writes it before
     * the renderer reads it, and destroys it. The renderer and its passes
     * read only the proxies, never the objects they were copied from, so
     * everything a frame draws with is fixed before the frame starts: a mesh
     * proxy's instance records and joint palette and a UI proxy's quads are
     * copies too, which the renderer uploads from here. Proxies are created
     * and destroyed between frames only (see @ref begin_frame). The helper
     * entries are non-owning back-pointers: a helper joins through its
     * constructor and leaves through its destructor. Passes read the
     * world only through the one a frame's @ref frame_context carries,
     * never through a global, so more than one world can exist in a process
     * (an editor's preview viewport, a render-to-texture scene) without them
     * interfering. Main-thread only, like the renderer.
     */
    struct render_world
    {
        /**
         * @brief Withdraws the drawable aspect reported to cameras: the
         *        renderer is going down, so there is no drawable to match.
         *        The proxies and the helper list are left as they are —
         *        every proxy's creator destroys it, and every helper removes
         *        itself.
         */
        void quit();

        /**
         * @brief Marks the frame the renderer is recording: until
         *        @ref end_frame, the passes hold pointers into the proxy
         *        storage, so creating or destroying a proxy asserts in
         *        debug builds.
         */
        void begin_frame() noexcept;

        /** @brief Ends the frame @ref begin_frame opened. */
        void end_frame() noexcept;

        /**
         * @brief Adds a mesh proxy drawing @p source at @p world and returns
         *        its handle. It is the last in draw order.
         */
        mesh_proxy_handle create_mesh(const mesh_description& source, const core::math::mat4& world);

        /** @brief Removes the mesh proxy @p mesh names. No-op for a stale handle. */
        void destroy_mesh(mesh_proxy_handle mesh);

        /**
         * @brief Replaces what @p mesh draws.
         *
         * Checks the geometry's vertex format against the material's
         * (@ref validate_vertex_format) when the description asks for it,
         * logging a mismatch once per geometry and material, and a missing
         * material once, and recomputes the world bounds.
         */
        void set_mesh_source(mesh_proxy_handle mesh, const mesh_description& source);

        /**
         * @brief Places @p mesh at @p world: rebuilds its PerDraw block
         *        (@ref make_per_draw_payload), its mirror flag and its world
         *        bounds.
         */
        void set_mesh_world(mesh_proxy_handle mesh, const core::math::mat4& world);

        /**
         * @brief Shows or hides @p mesh. A hidden proxy draws nothing but
         *        keeps its place in the draw order.
         */
        void set_mesh_visible(mesh_proxy_handle mesh, bool visible);

        /**
         * @brief Replaces the joint palette a skinning material draws @p mesh
         *        with (see @ref mesh_proxy::joints).
         */
        void set_mesh_joints(mesh_proxy_handle mesh, std::span<const core::math::mat4> joints);

        /**
         * @brief Captures an instance source's records and draw arguments
         *        into @p mesh's instance snapshot.
         *
         * @p records is every instance slot of the source, of which the first
         * @p args.instance_count are drawn, and [@p changed_begin,
         * @p changed_end) the slots written since the last capture; only
         * those are copied. A change in the number of slots copies them all.
         * The copied range joins the ones the renderer has yet to upload.
         */
        void write_mesh_instances(mesh_proxy_handle mesh,
                                  std::span<const mesh_instance> records,
                                  uint32_t changed_begin,
                                  uint32_t changed_end,
                                  const mesh_indirect_args& args);

        /** @brief The proxy @p mesh names, or @c nullptr for a stale handle. */
        mesh_proxy* mesh(mesh_proxy_handle mesh) noexcept;

        /** @copydoc mesh(mesh_proxy_handle) */
        const mesh_proxy* mesh(mesh_proxy_handle mesh) const noexcept;

        /**
         * @brief Every mesh proxy, in creation order — the order the scene,
         *        depth pre-pass and shadow passes walk them in.
         */
        std::span<const mesh_proxy> meshes() const noexcept
        {
            return m_meshes.values();
        }

        /** @brief The handle of the proxy at @p position of @ref meshes. */
        mesh_proxy_handle mesh_at(std::size_t position) const noexcept
        {
            return m_meshes.handle_at(position);
        }

        /**
         * @brief Adds a UI proxy holding @p data and returns its handle. It
         *        paints over every UI proxy created before it.
         */
        ui_proxy_handle create_ui_element(ui_element_data data);

        /** @brief Removes the UI proxy @p element names. No-op for a stale handle. */
        void destroy_ui_element(ui_proxy_handle element);

        /**
         * @brief Replaces what @p element draws with @p data and advances
         *        its revision, so the renderer uploads the quads again.
         */
        void set_ui_element(ui_proxy_handle element, ui_element_data data);

        /** @brief The proxy @p element names, or @c nullptr for a stale handle. */
        const ui_proxy* ui_element(ui_proxy_handle element) const noexcept;

        /** @brief Every UI proxy, in creation order — the order they paint in. */
        std::span<const ui_proxy> ui_elements() const noexcept
        {
            return m_ui_elements.values();
        }

        /** @brief The handle of the proxy at @p position of @ref ui_elements. */
        ui_proxy_handle ui_element_at(std::size_t position) const noexcept
        {
            return m_ui_elements.handle_at(position);
        }

        /**
         * @brief Adds a light proxy holding @p proxy and returns its handle.
         *        An @p enabled one is appended to @ref enabled_lights.
         */
        light_proxy_handle create_light(const light_proxy& proxy, bool enabled);

        /** @brief Removes the light proxy @p light names. No-op for a stale handle. */
        void destroy_light(light_proxy_handle light);

        /**
         * @brief Adds @p light to, or takes it out of, the enabled list.
         *
         * Disabling removes it from the list; enabling appends it at the
         * end, so a light enabled again packs after every light that stayed
         * enabled. A no-op when the light is already in the requested state
         * or @p light is stale.
         */
        void set_light_enabled(light_proxy_handle light, bool enabled);

        /** @brief Whether @p light is in the enabled list. */
        bool is_light_enabled(light_proxy_handle light) const noexcept;

        /** @brief The proxy @p light names, or @c nullptr for a stale handle. */
        light_proxy* light(light_proxy_handle light) noexcept;

        /** @copydoc light(light_proxy_handle) */
        const light_proxy* light(light_proxy_handle light) const noexcept;

        /**
         * @brief The enabled lights, in the order they were enabled; the
         *        scene pass packs them into the lights UBO in this order and
         *        the shadow passes take the first caster of each kind.
         */
        const std::vector<light_proxy_handle>& enabled_lights() const noexcept
        {
            return m_enabled_lights;
        }

        /**
         * @brief Replaces @p out with the enabled lights' proxies, in
         *        @ref enabled_lights order. The pointers stay valid until a
         *        light proxy is created or destroyed.
         */
        void collect_enabled_lights(std::vector<const light_proxy*>& out) const;

        /**
         * @brief Adds a camera proxy holding @p proxy and returns its handle.
         *        It is the newest camera, so it wins arbitration ties against
         *        every camera created before it.
         */
        camera_proxy_handle create_camera(const camera_proxy& proxy);

        /** @brief Removes the camera proxy @p camera names. No-op for a stale handle. */
        void destroy_camera(camera_proxy_handle camera);

        /** @brief The proxy @p camera names, or @c nullptr for a stale handle. */
        camera_proxy* camera(camera_proxy_handle camera) noexcept;

        /** @copydoc camera(camera_proxy_handle) */
        const camera_proxy* camera(camera_proxy_handle camera) const noexcept;

        /** @brief Number of camera proxies. */
        std::size_t camera_count() const noexcept
        {
            return m_cameras.size();
        }

        /**
         * @brief The camera the swapchain's primary view renders with: the
         *        highest-ranked enabled camera proxy on the swapchain —
         *        highest priority, a tie going to the camera tagged main,
         *        then to the most recently created — or an invalid handle
         *        when none is enabled. Cameras on a render texture never
         *        take the screen.
         *
         * The last view @ref collect_views lists is this camera's, so
         * destroying or disabling it promotes the runner-up on the next
         * frame with no bookkeeping by the owner.
         */
        camera_proxy_handle active_camera() const;

        /**
         * @brief The enabled camera tagged main with the highest priority
         *        (the most recently created on a tie), or an invalid handle.
         *
         * A way for game code to find "the player's camera" while a
         * higher-priority camera (a cutscene, a debug fly-cam) is
         * rendering.
         */
        camera_proxy_handle main_camera() const;

        /**
         * @brief Fills @p out with the views a frame renders, in the order
         *        the renderer renders them.
         *
         * Every enabled camera renders a view into its target (the
         * swapchain, @p drawable_width x @p drawable_height, or its render
         * texture) over its viewport rectangle, except that the cameras
         * drawing into the same rectangle of the same target compete: the
         * highest priority renders, a tie going to the camera tagged main,
         * then to the most recently created. So the cameras of an existing
         * scene, all on the whole swapchain, render one view between them,
         * while two cameras with a half of the swapchain each split the
         * screen. The views on render textures come first, so the views that
         * sample them see this frame's image; within each group the lower
         * ranked render first, so the highest-ranked view is drawn on top
         * where rectangles overlap and a texture view that samples another
         * renders after it when it ranks higher. When no camera renders to
         * the swapchain, a camera-less view covers it, so the swapchain is
         * always drawn (and cleared). A view is at least one pixel across;
         * its rectangle is clamped to its target. The last view of the list
         * is the primary one — @ref active_camera's, or the camera-less one —
         * whose camera the once-per-frame stages use.
         */
        void collect_views(std::vector<view>& out, uint32_t drawable_width, uint32_t drawable_height) const;

        /**
         * @brief Records the drawable's width / height, the aspect cameras
         *        follow, and advances @ref aspect_revision.
         *
         * A value of zero or less clears the recorded aspect and leaves the
         * revision alone: there is no drawable for a camera to follow.
         */
        void set_drawable_aspect(float aspect_ratio);

        /** @brief The last value given to @ref set_drawable_aspect, or 0 when none is known. */
        float drawable_aspect() const noexcept
        {
            return m_drawable_aspect;
        }

        /**
         * @brief Advances whenever @ref set_drawable_aspect reports a
         *        drawable, so a camera's owner hands the aspect to the
         *        camera (@ref camera::set_aspect_ratio) once per report.
         */
        uint64_t aspect_revision() const noexcept
        {
            return m_aspect_revision;
        }

        /**
         * @brief Adds @p h to the helper list the debug UI walks to toggle
         *        visibility and @ref debug_draw::update_helpers to rebuild
         *        the gizmos. Called by @ref debug_draw::helper's
         *        constructor; not for callers to use directly.
         */
        void add_helper(debug_draw::helper& h);

        /** @brief Removes @p h from the helper list. No-op if absent. */
        void remove_helper(debug_draw::helper& h);

        /** @brief The helpers alive right now, in construction order. */
        const std::vector<debug_draw::helper*>& helpers() const noexcept
        {
            return m_helpers;
        }

        /**
         * @brief Sets (or, with @c nullptr, clears) the scene's environment
         *        probe. Non-owning: @p probe must outlive the scene (or be
         *        cleared first). Only stored here; the renderer points the
         *        skybox and the standard materials at it.
         */
        void set_environment(const environment_probe* probe);

        /** @brief The scene's environment probe, or @c nullptr. */
        const environment_probe* environment() const noexcept
        {
            return m_environment;
        }

        /**
         * @brief The environment probe's BRDF look-up table (a 2D
         *        scale/bias table), or an invalid handle when no probe is
         *        set.
         */
        gpu::texture environment_brdf_lut() const;

        /**
         * @brief Sets the scene-wide atmospheric fog, copied into the
         *        frame context every frame. A @ref fog_settings with
         *        @ref fog_mode::none (the default) disables fog.
         */
        void set_fog(const fog_settings& fog);

        /** @brief The scene-wide atmospheric fog currently in effect. */
        const fog_settings& fog() const noexcept
        {
            return m_fog;
        }

    private:
        core::dense_pool<mesh_proxy, mesh_proxy_tag> m_meshes;
        core::dense_pool<ui_proxy, ui_proxy_tag> m_ui_elements;

        core::dense_pool<light_proxy, light_proxy_tag> m_lights;
        std::vector<light_proxy_handle> m_enabled_lights;
        core::dense_pool<camera_proxy, camera_proxy_tag> m_cameras;
        std::vector<debug_draw::helper*> m_helpers;

        // The last value handed to set_drawable_aspect; 0 when none is
        // known yet (see drawable_aspect).
        float m_drawable_aspect{0.0f};
        uint64_t m_aspect_revision{0};

        // Set between begin_frame and end_frame.
        bool m_in_frame{false};

        // The active environment probe, or null. Non-owning.
        const environment_probe* m_environment{nullptr};

        // Defaults to fog_mode::none (disabled).
        fog_settings m_fog{};
    };
} // namespace rendering_engine
