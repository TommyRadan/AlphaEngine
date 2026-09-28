// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file light_component.hpp
 * @brief Component that binds a light source to a node.
 */

#pragma once

#include <cstdint>
#include <memory>

#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    struct render_world;
}

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a light source.
     *
     * Owns the light's settings — a @ref rendering_engine::light of any kind,
     * on the heap — and the @ref rendering_engine::light_proxy the renderer
     * lights the frame with, which lives in @c owner.scene()->world() and is
     * named by a handle. @ref on_attach creates the proxy (enabled when the
     * settings are) and @ref on_destroy — called before the component is
     * freed with the node (or removed) — destroys it.
     *
     * The proxy follows the node through @ref extract, which the world's
     * render extraction (@ref runtime::extract_render_proxies) calls once per
     * frame, after every update has moved the nodes: a point light sits at
     * the node's world translation, a directional light travels along the
     * node's world forward — the +X axis of the engine convention
     * (core/math/math.hpp), so an identity-oriented node shines horizontally
     * and @c node::look_at aims it — and a spot light follows both (see
     * @ref rendering_engine::place_light). An ambient light has no spatial
     * term. The pose is only re-derived when the node's world matrix changed,
     * while the settings are copied every frame, so an edit made anywhere
     * (the inspector, a script) reaches the next frame.
     *
     * @ref on_active_changed enables / disables the light with its node, so a
     * disabled subtree stops lighting (and shadowing) the scene as well as
     * stopping its updates. The settings' own enabled flag, toggled directly,
     * takes effect at the next extraction.
     */
    struct light_component
    {
        /** @brief Empty component — owns no light. */
        light_component() = default;

        /** @brief Takes ownership of @p light, which gets its proxy on attach. */
        explicit light_component(std::unique_ptr<rendering_engine::light> light);

        /**
         * @brief Creates the light's proxy in @c owner.scene()->world(), if
         *        the component owns a light and the node has a scene.
         *
         * Called by @ref node::add_component, once, right after the
         * component is attached. The proxy joins the world's enabled lights
         * at the end when the settings are enabled.
         */
        void on_attach(node& owner);

        /** @brief Destroys the light's proxy. Called before the component is freed. */
        void on_destroy();

        /**
         * @brief Writes the settings and, when @p owner moved, the pose into
         *        the light's proxy. Called once per frame by the render
         *        extraction; a component without a proxy does nothing.
         */
        void extract(const node& owner);

        /**
         * @brief Takes the light out of the world's enabled lights when the
         *        owning node is disabled, and puts it back at the end when
         *        re-enabled.
         *
         * Called by @ref node::set_active. The settings are untouched but for
         * their enabled flag; see @ref rendering_engine::light::set_enabled.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief A new component owning a light of the same kind and settings
         *        (colour, intensity, attenuation, cone and shadow casting),
         *        for @c scene::clone.
         *
         * The copy has no proxy, same as a fresh light_component; @ref
         * on_attach creates one when the copy's node gets it, and the cloned
         * node's active state then applies as usual.
         */
        light_component clone() const;

        /** @brief The owned light's settings, or @c nullptr for an empty component. */
        rendering_engine::light* get() const noexcept
        {
            return m_light.get();
        }

        /** @brief The light's proxy in its scene's world; invalid until attached. */
        rendering_engine::light_proxy_handle proxy() const noexcept
        {
            return m_proxy;
        }

    private:
        std::unique_ptr<rendering_engine::light> m_light;

        // The world the proxy lives in (the owning scene's) and the proxy.
        rendering_engine::render_world* m_world{nullptr};
        rendering_engine::light_proxy_handle m_proxy{};

        // The world-matrix stamp of the node the proxy was last placed from.
        uint64_t m_placed_version{0};
    };
} // namespace runtime
