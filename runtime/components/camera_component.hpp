// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file camera_component.hpp
 * @brief Component that binds a camera to a node.
 */

#pragma once

#include <memory>

#include <rendering_engine/camera/camera.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a camera.
     *
     * Owns a @ref rendering_engine::camera (perspective or orthographic) on the
     * heap. @ref on_attach parents the camera's transform under the node — so
     * the camera's world pose, position and orientation alike, is the node's
     * composed with the camera's own local offset, with no per-frame glue: a
     * camera under an animated node (a turntable rig, a chase cam) follows it
     * through the transform parent chain — and attaches the camera to the
     * renderer's camera registry as a candidate for the active camera.
     * Orient it with @c node::look_at (or @c camera::look_at, which accounts
     * for the parent), and rank it against other cameras with
     * @c camera::set_priority / @c set_main.
     *
     * @ref on_active_changed enables / disables the camera with its node, so
     * a disabled subtree's camera drops out of the arbitration and the next
     * candidate renders; @ref on_destroy detaches it, so destroying the node
     * (or removing the component) promotes the runner-up automatically.
     *
     * The camera lives behind a @c unique_ptr so its address — held by the
     * registry — survives the component being relocated within its pool.
     */
    struct camera_component
    {
        /** @brief Empty component — owns no camera. */
        camera_component() = default;

        /** @brief Takes ownership of @p camera. */
        explicit camera_component(std::unique_ptr<rendering_engine::camera> camera);

        /** @brief Parents the camera under @p owner and attaches it to the camera registry. */
        void on_attach(node& owner);

        /** @brief Detaches the camera from the registry and unparents it. */
        void on_destroy();

        /**
         * @brief Enables the camera when the owning node becomes effectively
         *        active and disables it when it does not, so the registry's
         *        arbitration skips a disabled node's camera.
         *
         * Called by @ref node::set_active.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief A new component owning a camera of the same kind and
         *        settings — projection, local offset, priority and main flag —
         *        for @c scene::clone.
         *
         * The copy attaches to the registry like any new camera (so, as the
         * most recently attached of equal rank, it wins a tie) and starts
         * enabled; the cloned node's active state then applies as usual. A
         * camera type other than the built-in perspective and orthographic
         * ones cannot be copied, and its clone is empty (with a warning).
         */
        camera_component clone() const;

        /** @brief The owned camera, or @c nullptr for an empty component. */
        rendering_engine::camera* get() const noexcept
        {
            return m_camera.get();
        }

    private:
        std::unique_ptr<rendering_engine::camera> m_camera;
    };
} // namespace runtime
