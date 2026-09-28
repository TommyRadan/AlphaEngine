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
     * - the lights, in attach order (@ref lights), packed into the lights
     *   UBO in that order by the scene pass;
     * - the cameras (@ref cameras) and the arbitration that picks the
     *   camera a frame renders with (@ref active_camera);
     * - the debug-draw helper list the debug UI walks to toggle visibility
     *   (@ref helpers);
     * - the environment probe the skybox and the standard materials use;
     * - the scene-wide atmospheric fog.
     *
     * Every entry is a non-owning back-pointer: a light or camera joins
     * through its own @ref light::attach / @ref camera::attach (whoever
     * constructs it — a component bridge, a game module — registers it with
     * a specific world) and a helper through its constructor, and each
     * leaves through the matching @c detach / destructor. Passes read
     * lights and cameras only through the world a frame's
     * @ref frame_context carries, never through a global, so more than one
     * world can exist in a process (an editor's preview viewport, a
     * render-to-texture scene) without them interfering. Main-thread only,
     * like the renderer.
     */
    struct render_world
    {
        /**
         * @brief Withdraws the drawable aspect handed to attaching cameras:
         *        the renderer is going down, so there is no drawable to
         *        match. The registries are left as they are — every
         *        renderable, light, camera and helper unregisters itself.
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

        /**
         * @brief Appends @p l to the light list. The primitive behind
         *        @ref light::attach / @ref light::set_enabled; call those
         *        instead.
         */
        void add_light(light& l);

        /** @brief Removes @p l from the light list. No-op if absent. */
        void remove_light(light& l);

        /** @brief Every attached, enabled light, in attach order; the scene pass packs these into the lights UBO. */
        const std::vector<light*>& lights() const noexcept
        {
            return m_lights;
        }

        /**
         * @brief Appends @p cam to the camera list, moving it to the back
         *        first if it is already listed, and applies the current
         *        drawable aspect to it. The primitive behind
         *        @ref camera::attach; call that instead.
         */
        void add_camera(camera& cam);

        /** @brief Removes @p cam from the camera list. No-op if absent. */
        void remove_camera(camera& cam);

        /** @brief Every attached camera, in attach order. */
        const std::vector<camera*>& cameras() const noexcept
        {
            return m_cameras;
        }

        /**
         * @brief The camera this frame renders with: the highest-priority
         *        attached, enabled camera (a priority tie goes to the
         *        camera tagged main, then to the most recently attached),
         *        or @c nullptr when none is attached and enabled.
         *
         * @ref renderer::render evaluates this once per frame into
         * @c frame_context::active_camera, so destroying or disabling the
         * winner promotes the runner-up on the next frame with no
         * bookkeeping by the owner.
         */
        camera* active_camera() const;

        /**
         * @brief The attached, enabled camera tagged main with the highest
         *        priority (the most recently attached on a tie), or
         *        @c nullptr.
         *
         * A way for game code to find "the player's camera" while a
         * higher-priority camera (a cutscene, a debug fly-cam) is
         * rendering.
         */
        camera* main_camera() const;

        /**
         * @brief Reports the drawable's width / height to every attached
         *        camera and to cameras attached later.
         *
         * A value of zero or less clears the recorded aspect (nothing is
         * forwarded, later attaches leave the camera's aspect alone).
         */
        void set_drawable_aspect(float aspect_ratio);

        /** @brief The last value given to @ref set_drawable_aspect, or 0 when none is known. */
        float drawable_aspect() const noexcept
        {
            return m_drawable_aspect;
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

        std::vector<light*> m_lights;
        std::vector<camera*> m_cameras;
        std::vector<debug_draw::helper*> m_helpers;

        // The last value handed to set_drawable_aspect; 0 when none is
        // known yet (see drawable_aspect).
        float m_drawable_aspect{0.0f};

        // The active environment probe, or null. Non-owning.
        const environment_probe* m_environment{nullptr};

        // Defaults to fog_mode::none (disabled).
        fog_settings m_fog{};
    };
} // namespace rendering_engine
