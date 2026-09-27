// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file audio_listener_component.hpp
 * @brief Component that makes a node the audio subsystem's listener.
 */

#pragma once

#include <cstdint>

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a candidate audio listener.
     *
     * Registers with @c core::audio (@ref on_attach) as a candidate for the
     * one active listener 3D playback is mixed against, and keeps its
     * world-space position and right axis in step with the node
     * (@ref on_update). Arbitration is the audio subsystem's: among the
     * attached, enabled listeners the most recently attached wins, mirroring
     * the camera registry's arbitration in spirit
     * (rendering_engine/camera/camera_registry.hpp) but without a priority
     * ranking — so destroying or disabling the winner (@ref on_destroy /
     * @ref on_active_changed) promotes whichever candidate attached before
     * it, with no bookkeeping of its own.
     *
     * Holds only an opaque token (no heap payload), so unlike
     * @c camera_component / @c light_component it needs no @c unique_ptr for
     * address stability — the token stays valid across the component being
     * relocated within its pool.
     */
    struct audio_listener_component
    {
        audio_listener_component() = default;

        /** @brief Registers this node as a listener candidate. */
        void on_attach(node& owner);

        /** @brief Pushes the node's world-space position and right axis to the audio subsystem. */
        void on_update(node& owner);

        /** @brief Unregisters the listener. */
        void on_destroy();

        /**
         * @brief Enables/disables this listener for arbitration when the
         *        owning node is enabled/disabled.
         *
         * Called by @ref node::set_active.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief A new, unattached listener component, for @c scene::clone.
         *
         * The copy attaches like any new listener when its cloned node is
         * added to the tree (so, as the most recently attached, it wins a
         * tie) and starts enabled; the cloned node's active state then
         * applies as usual.
         */
        audio_listener_component clone() const;

        /** @brief Whether this component currently holds a listener registration. */
        bool is_attached() const noexcept
        {
            return m_token != 0;
        }

    private:
        std::uint32_t m_token{0}; // a core::audio::listener_token; 0 names none
    };
} // namespace runtime
