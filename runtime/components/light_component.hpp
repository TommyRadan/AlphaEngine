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
     * Owns a @ref rendering_engine::light (any kind) on the heap. A light adds
     * itself to the renderer's light registry in its own constructor and
     * removes itself in its destructor, so this component needs no attach /
     * detach plumbing — building it registers the light, destroying the node
     * (or removing the component) frees and unregisters it. The light lives
     * behind a @c unique_ptr so the registry's back-pointer stays valid as the
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

        /** @brief Takes ownership of @p light (already registered on construction). */
        explicit light_component(std::unique_ptr<rendering_engine::light> light);

        /** @brief Syncs the light's position / direction from @p owner's world transform. */
        void on_update(node& owner);

        /**
         * @brief Takes the light out of the renderer's registry when the
         *        owning node is disabled, and puts it back when re-enabled.
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
         * The copy registers itself like any new light and starts enabled;
         * the cloned node's active state then applies as usual.
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
