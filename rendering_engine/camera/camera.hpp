// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    /**
     * @brief A viewpoint: a transform plus a projection, and a candidate for
     *        the camera a frame renders with.
     *
     * Orientation is the quaternion of @ref transform, in the engine's
     * world convention (core/math/math.hpp): the camera looks along its
     * transform's +X with +Z up, so a fresh camera faces +X. @ref look_at
     * sets it; the transform's own setters, @ref core::transform::get_forward
     * and its world matrix all agree with it, and a camera transform parented
     * under a node composes like any other. The view matrix is derived from
     * the transform's world matrix on every call — there is no view cache to
     * invalidate, so a direct @c transform.set_position is never stale — and
     * the projection is cached until one of the subclass setters invalidates
     * it.
     *
     * Arbitration: a camera only renders while attached to the camera
     * registry (camera_registry.hpp). Among the attached, enabled cameras the
     * highest priority wins; a priority tie goes to the camera tagged main,
     * then to the most recently attached. The renderer picks the winner once
     * per frame, so destroying or disabling it promotes the next. A camera
     * is non-copyable because the registry holds its address; the destructor
     * detaches it.
     */
    struct camera
    {
        camera();
        virtual ~camera();

        camera(const camera&) = delete;
        camera& operator=(const camera&) = delete;

        core::transform transform;

        /**
         * @brief Orients the camera so it faces @p target in world space, with
         *        @p up (the engine's +Z by default) as the reference up.
         *
         * A target on the up axis takes the fallback up of
         * @ref core::math::reference_up; a target at the camera's position
         * leaves the orientation unchanged. Under a parented transform the
         * target and up are re-expressed in the parent's frame, so the camera
         * faces the world-space target exactly.
         */
        void look_at(const core::math::vec3& target, const core::math::vec3& up = core::math::world_up);

        /**
         * @brief World-to-view matrix, derived from the transform's world
         *        matrix: its translation is the eye, its +X column the
         *        forward and its +Z column the up (each normalised, so the
         *        transform's scale does not distort the view). A degenerate
         *        (zero-scale) column falls back to the identity orientation
         *        rather than producing NaNs.
         */
        core::math::mat4 get_view_matrix() const;

        /** @brief Marks the cached projection stale; the subclass setters call this. */
        void invalidate_projection_matrix();
        virtual const core::math::mat4 get_projection_matrix() const = 0;

        // Follows the drawable's width / height. The camera registry calls
        // this on every attached camera whenever the renderer reports a new
        // drawable size (init, resize) and on a camera as it attaches, so a
        // resize does not stretch the image. Cameras whose projection has no
        // aspect (orthographic magnifications) ignore it.
        virtual void set_aspect_ratio(float aspect_ratio)
        {
            (void)aspect_ratio;
        }

        const core::math::frustum get_frustum() const;

        // --- Arbitration -------------------------------------------------

        /**
         * @brief Adds the camera to the registry as a candidate for the active
         *        camera. Attaching an already attached camera moves it to the
         *        back, so it wins ties against its peers.
         */
        void attach();

        /** @brief Removes the camera from the registry. No-op when not attached. */
        void detach();

        bool is_attached() const;

        /**
         * @brief An attached but disabled camera stays registered but never
         *        renders. Cameras start enabled; @c camera_component follows
         *        its node's active state with this.
         */
        void set_enabled(bool enabled) noexcept;
        bool is_enabled() const noexcept;

        /** @brief Higher renders first; the default is 0. */
        void set_priority(int priority) noexcept;
        int get_priority() const noexcept;

        /**
         * @brief Tags the camera as the main one: it wins priority ties and is
         *        what @ref main_camera returns. Off by default.
         */
        void set_main(bool main) noexcept;
        bool is_main() const noexcept;

        /**
         * @brief Layer bits this camera renders. The scene pass skips a
         *        renderable whenever @c (renderable::layer_mask &
         *        culling_mask()) == 0, the same way it skips one wholly
         *        outside the frustum. Defaults to @ref layer_all, so a
         *        fresh camera renders every layer, including the editor
         *        one (@ref layer_editor) the debug helpers use; clear that
         *        bit to hide them from a gameplay camera.
         */
        void set_culling_mask(uint32_t mask) noexcept;
        uint32_t culling_mask() const noexcept;

    protected:
        mutable core::math::mat4 m_projection;
        mutable bool m_is_projection_matrix_dirty;

    private:
        bool m_enabled{true};
        bool m_main{false};
        int m_priority{0};
        uint32_t m_culling_mask{layer_all};
    };
} // namespace rendering_engine
