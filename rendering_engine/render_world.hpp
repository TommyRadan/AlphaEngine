// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_world.hpp
 * @brief What the renderer draws: renderables, light and camera proxies, the
 *        environment probe and the scene-wide fog.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <core/dense_pool.hpp>
#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    struct environment_probe;
    struct renderable;

    namespace debug_draw
    {
        struct helper;
    }

    /**
     * @brief The renderer's view of the world.
     *
     * Owned by the @ref renderer, which reads it once per frame to fill the
     * @ref frame_context and hands its renderable registries to the passes
     * that draw them. It owns:
     *
     * - the scene, UI and debug renderable registries — non-owning, in
     *   registration order, which is the dispatch order within a pass;
     * - the light proxies (@ref light_proxy), and the order the enabled ones
     *   are packed into the lights UBO in (@ref enabled_lights);
     * - the camera proxies (@ref camera_proxy) and the arbitration that picks
     *   the camera a frame renders with (@ref active_camera);
     * - the debug-draw helper list the debug UI walks to toggle visibility
     *   (@ref helpers);
     * - the environment probe the skybox and the standard materials use;
     * - the scene-wide atmospheric fog.
     *
     * A proxy is a plain copy the world owns, addressed by a handle: its
     * creator (a runtime light or camera component) creates it, enables it,
     * writes it once per frame, before the renderer reads it, and destroys
     * it. The renderer and its passes read only the proxies, never the
     * objects they were copied from, so everything a frame draws with is
     * fixed before the frame starts. Proxies are created and destroyed
     * between frames only (see @ref begin_frame). The renderable and helper
     * entries are non-owning back-pointers: a renderable joins through the
     * register calls and a helper through its constructor, and each leaves
     * through the matching unregister call / destructor. Passes read the
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
         *        The registries and proxies are left as they are — every
         *        renderable and helper unregisters itself, and every proxy's
         *        creator destroys it.
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
         * @brief Adds @p r to the scene-pass registry.
         *
         * The pointer is non-owning; callers must
         * @ref unregister_scene_renderable before destroying the
         * renderable. Registration order is preserved and is the dispatch
         * order during the scene pass.
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
         * debug builds; release builds drop the debug pass from the pass
         * list entirely so registrations are inert. Same ownership rules
         * as the scene variant.
         */
        void register_debug_renderable(renderable* r);

        /** @brief Removes @p r from the debug-pass registry; no-op if absent. */
        void unregister_debug_renderable(renderable* r);

        /** @brief The scene-pass registry, which the shadow passes walk too. */
        const std::vector<renderable*>& scene_renderables() const noexcept
        {
            return m_scene_renderables;
        }

        /** @brief The UI-pass registry. */
        const std::vector<renderable*>& ui_renderables() const noexcept
        {
            return m_ui_renderables;
        }

        /** @brief The debug-pass registry. */
        const std::vector<renderable*>& debug_renderables() const noexcept
        {
            return m_debug_renderables;
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
         * @brief The camera this frame renders with: the highest-priority
         *        enabled camera proxy (a priority tie goes to the camera
         *        tagged main, then to the most recently created), or an
         *        invalid handle when none is enabled.
         *
         * @ref renderer::render evaluates this once per frame into
         * @c frame_context::active_camera, so destroying or disabling the
         * winner promotes the runner-up on the next frame with no
         * bookkeeping by the owner.
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
         *        visibility. Called by @ref debug_draw::helper's
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
        std::vector<renderable*> m_scene_renderables;
        std::vector<renderable*> m_ui_renderables;
        std::vector<renderable*> m_debug_renderables;

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
