// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_world.hpp
 * @brief What the renderer draws: renderables, lights, cameras, the
 *        environment probe and the scene-wide fog.
 */

#pragma once

#include <vector>

#include <rendering_engine/fog.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    struct camera;
    struct environment_probe;
    struct light;
    struct renderable;

    /**
     * @brief The renderer's view of the world.
     *
     * Owned by the @ref renderer, which reads it once per frame to fill the
     * @ref frame_context and hands its renderable registries to the passes
     * that draw them. It holds:
     *
     * - the scene, UI and debug renderable registries — non-owning, in
     *   registration order, which is the dispatch order within a pass;
     * - the lights and cameras, and the arbitration that picks the camera a
     *   frame renders with (@ref active_camera);
     * - the environment probe the skybox and the standard materials use;
     * - the scene-wide atmospheric fog.
     *
     * Lights and cameras keep their process-wide registries
     * (lighting/light.hpp, camera/camera_registry.hpp): a light or camera
     * attaches through its own @c attach whether or not a renderer is up —
     * components and the device-free tests create them with no renderer at
     * all — so the world reads those registries rather than holding copies,
     * and the drawable aspect it reports reaches every attached camera
     * through them. The world owns no GPU resource. Main-thread only, like
     * the renderer.
     */
    struct render_world
    {
        /**
         * @brief Withdraws the drawable aspect handed to attaching cameras:
         *        the renderer is going down, so there is no drawable to
         *        match. The registries are left as they are — every
         *        renderable unregisters itself.
         */
        void quit();

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

        /** @brief Every attached light, in attach order (see @ref registered_lights). */
        const std::vector<light*>& lights() const;

        /** @brief Every attached camera, in attach order (see @ref registered_cameras). */
        const std::vector<camera*>& cameras() const;

        /**
         * @brief The camera this frame renders with: the arbitration's
         *        winner (the highest-priority attached, enabled camera; see
         *        @ref rendering_engine::active_camera), or @c nullptr.
         */
        camera* active_camera() const;

        /**
         * @brief Reports the drawable's width / height to every attached
         *        camera and to cameras attached later (see
         *        @ref rendering_engine::set_drawable_aspect).
         */
        void set_drawable_aspect(float aspect_ratio);

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

        // The active environment probe, or null. Non-owning.
        const environment_probe* m_environment{nullptr};

        // Defaults to fog_mode::none (disabled).
        fog_settings m_fog{};
    };
} // namespace rendering_engine
