// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file light_component.hpp
 * @brief Component that binds a light source to a node.
 */

#pragma once

#include <memory>

#include <rendering_engine/lighting/light.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a light source.
     *
     * Owns a @ref rendering_engine::light (any kind) on the heap, built
     * unattached. @ref on_attach attaches it to @c owner.scene()->world()
     * (see @ref rendering_engine::light::attach), and @ref on_destroy —
     * called before the light is freed with the node (or the component
     * removed) — detaches it. The light lives behind a @c unique_ptr so its
     * address, which the world holds while attached, stays valid as the
     * component is relocated within its pool.
     *
     * @ref on_update keeps the light's spatial fields in step with the node: a
     * point light's position follows the node's world translation, a
     * directional light's direction follows the node's world forward — the
     * +X axis of the engine convention (core/math/math.hpp), so an
     * identity-oriented node shines horizontally and @c node::look_at aims it
     * — and a spot light follows both: position from the node's world
     * translation, direction from its world forward, the same as the other
     * two. A node whose scale collapses the forward axis to zero keeps the
     * light's last direction rather than writing a NaN. An ambient light has
     * no spatial term and is left untouched.
     *
     * @ref on_active_changed enables / disables the light with its node, so a
     * disabled subtree stops lighting (and shadowing) the scene as well as
     * stopping its updates.
     */
    struct light_component
    {
        /** @brief Empty component — owns no light. */
        light_component() = default;

        /** @brief Takes ownership of @p light, unattached. */
        explicit light_component(std::unique_ptr<rendering_engine::light> light);

        /**
         * @brief Attaches the light to @c owner.scene()->world(), if the
         *        component owns one and the node has a scene.
         *
         * Called by @ref node::add_component, once, right after the
         * component is attached.
         */
        void on_attach(node& owner);

        /** @brief Detaches the light. Called before the component is freed. */
        void on_destroy();

        /** @brief Syncs the light's position / direction from @p owner's world transform. */
        void on_update(node& owner);

        /**
         * @brief Takes the light out of its world when the owning node is
         *        disabled, and puts it back when re-enabled.
         *
         * Called by @ref node::set_active. The light object and its settings
         * are untouched; see @ref rendering_engine::light::set_enabled.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief A new component owning a light of the same kind and settings
         *        (colour, intensity, direction or position and attenuation,
         *        shadow casting), for @c scene::clone.
         *
         * The copy is unattached, same as a fresh light_component; @ref
         * on_attach registers it when the copy's node gets one, and the
         * cloned node's active state then applies as usual.
         */
        light_component clone() const;

        /** @brief The owned light, or @c nullptr for an empty component. */
        rendering_engine::light* get() const noexcept
        {
            return m_light.get();
        }

    private:
        std::unique_ptr<rendering_engine::light> m_light;
    };
} // namespace runtime
