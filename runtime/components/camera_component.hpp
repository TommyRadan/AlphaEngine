// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file camera_component.hpp
 * @brief Component that binds a camera to a node.
 */

#pragma once

#include <cstdint>
#include <memory>

#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    struct render_world;
}

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a camera.
     *
     * Owns the camera's settings — a @ref rendering_engine::camera
     * (perspective or orthographic) on the heap, its lens and its rank —, a
     * local @ref offset from the node, and the
     * @ref rendering_engine::camera_proxy the renderer views the frame
     * through, which lives in @c owner.scene()->world() and is named by a
     * handle. @ref on_attach creates the proxy, a candidate for a view, and
     * hands the camera the aspect of the rectangle it renders into (its
     * viewport's share of the world's drawable, or of its render texture).
     *
     * The proxy follows the node through @ref extract, which the world's
     * render extraction (@ref runtime::extract_render_proxies) calls once per
     * frame, after every update has moved the nodes: the camera's world pose
     * is the node's composed with the offset (@ref world_matrix), so a camera
     * under an animated node (a turntable rig, a chase cam) follows it. The
     * view is re-derived only when the node or the offset moved, and the
     * projection and frustum only when the pose or the lens (including a
     * new aspect of its rectangle) changed; the rank, culling mask, target,
     * viewport and UI flag are copied every frame. Orient it with
     * @c node::look_at, rank it against other cameras with
     * @c camera::set_priority / @c set_main, and point it at a render
     * texture or a part of the screen with @c camera::set_target /
     * @c set_viewport.
     *
     * @ref on_active_changed enables / disables the camera with its node, so
     * a disabled subtree's camera drops out of the arbitration and the next
     * candidate renders; @ref on_destroy destroys the proxy, so destroying
     * the node (or removing the component) promotes the runner-up
     * automatically.
     */
    struct camera_component
    {
        /** @brief Empty component — owns no camera. */
        camera_component() = default;

        /** @brief Takes ownership of @p camera, which gets its proxy on attach. */
        explicit camera_component(std::unique_ptr<rendering_engine::camera> camera);

        /**
         * @brief Creates the camera's proxy in @c owner.scene()->world() and
         *        hands the camera the aspect of the rectangle it renders
         *        into.
         */
        void on_attach(node& owner);

        /** @brief Destroys the camera's proxy. Called before the component is freed. */
        void on_destroy();

        /**
         * @brief Writes the camera's pose (when @p owner or the offset moved),
         *        lens and rank into its proxy. Called once per frame by the
         *        render extraction; a component without a proxy does nothing.
         */
        void extract(const node& owner);

        /**
         * @brief Enables the camera when the owning node becomes effectively
         *        active and disables it when it does not, so the world's
         *        arbitration skips a disabled node's camera.
         *
         * Called by @ref node::set_active.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief A new component owning a camera of the same kind and
         *        settings — projection, offset, priority, main flag, target,
         *        viewport and UI flag — for @c scene::clone.
         *
         * The copy gets its proxy like any new camera (so, as the most
         * recently created of equal rank, it wins a tie) and starts enabled;
         * the cloned node's active state then applies as usual. A camera
         * type other than the built-in perspective and orthographic ones
         * cannot be copied, and its clone is empty (with a warning).
         */
        camera_component clone() const;

        /** @brief The owned camera's settings, or @c nullptr for an empty component. */
        rendering_engine::camera* get() const noexcept
        {
            return m_camera.get();
        }

        /** @brief The camera's proxy in its scene's world; invalid until attached. */
        rendering_engine::camera_proxy_handle proxy() const noexcept
        {
            return m_proxy;
        }

        /**
         * @brief The camera's pose relative to its node (identity unless set).
         *        Never parented: the node's world matrix is composed with it
         *        at extraction.
         */
        core::transform& offset() noexcept
        {
            return m_offset;
        }

        /** @copydoc offset() */
        const core::transform& offset() const noexcept
        {
            return m_offset;
        }

        /** @brief The camera's world matrix under @p owner: the node's world matrix times the offset. */
        core::math::mat4 world_matrix(const node& owner) const;

    private:
        std::unique_ptr<rendering_engine::camera> m_camera;
        core::transform m_offset;

        // The world the proxy lives in (the owning scene's) and the proxy.
        rendering_engine::render_world* m_world{nullptr};
        rendering_engine::camera_proxy_handle m_proxy{};

        // The world-matrix stamps of the node and the offset the proxy's
        // view was last derived from, the drawable-aspect report the camera
        // last took, and the aspect of its rectangle last handed to it.
        uint64_t m_node_version{0};
        uint64_t m_offset_version{0};
        uint64_t m_aspect_revision{0};
        float m_applied_aspect{0.0f};
    };
} // namespace runtime
