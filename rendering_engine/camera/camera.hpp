// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

#include <core/math/math.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    class render_texture;

    /**
     * @brief A camera's lens and its rank among cameras: the projection, the
     *        layers it renders, and its enabled flag, priority and main tag.
     *
     * The settings of a viewpoint, not the viewpoint itself: a camera carries
     * no pose and knows no world. Its owner (a runtime::camera_component)
     * keeps a @ref camera_proxy in a @ref render_world, places it from its
     * node — the camera looks along its world +X with +Z up, the engine's
     * world convention (core/math/math.hpp), so the view comes from
     * @ref view_matrix_from_world — and copies these settings into it before
     * each frame. The projection is cached until one of the subclass setters
     * invalidates it.
     *
     * A camera renders a view (see @ref view) into its target — the
     * window's swapchain by default, or a @ref render_texture
     * (@ref set_target) — over a rectangle of it (@ref set_viewport, the
     * whole target by default), so two cameras with a rectangle each split
     * the screen and a camera on a render texture feeds a material.
     *
     * Arbitration: among the enabled cameras of a world that draw into the
     * same rectangle of the same target the highest priority renders; a
     * priority tie goes to the camera tagged main, then to the one whose
     * proxy was created last (@ref render_world::collect_views). The
     * renderer picks the winners once per frame, so disabling one (or
     * dropping its proxy) promotes the next. Non-copyable, so its projection
     * cache and settings have one owner.
     */
    struct camera
    {
        camera();
        virtual ~camera() = default;

        camera(const camera&) = delete;
        camera& operator=(const camera&) = delete;

        /** @brief Marks the cached projection stale; the subclass setters call this. */
        void invalidate_projection_matrix();
        virtual const core::math::mat4 get_projection_matrix() const = 0;

        // Follows the width / height of the camera's rectangle. The camera's
        // owner hands it the aspect of that rectangle — of the drawable its
        // render_world reports (@ref render_world::drawable_aspect) for a
        // camera on the swapchain, of its render texture otherwise — when the
        // camera joins the world and again whenever it changes (a resize, a
        // new target or viewport), so the image is never stretched. Cameras
        // whose projection has no aspect (orthographic magnifications)
        // ignore it.
        virtual void set_aspect_ratio(float aspect_ratio)
        {
            (void)aspect_ratio;
        }

        /**
         * @brief A disabled camera stays in its world but never renders.
         *        Cameras start enabled; @c camera_component follows its
         *        node's active state with this.
         */
        void set_enabled(bool enabled) noexcept;
        bool is_enabled() const noexcept;

        /**
         * @brief Higher wins the arbitration, and is drawn after (on top of)
         *        the views of lower rank; the default is 0.
         */
        void set_priority(int priority) noexcept;
        int get_priority() const noexcept;

        /**
         * @brief Tags the camera as the main one: it wins priority ties and is
         *        what @ref render_world::main_camera returns. Off by default.
         */
        void set_main(bool main) noexcept;
        bool is_main() const noexcept;

        /**
         * @brief Layer bits this camera renders. The scene pass skips a
         *        mesh proxy whenever @c (mesh_description::layer_mask &
         *        culling_mask()) == 0, the same way it skips one wholly
         *        outside the frustum. Defaults to @ref layer_all, so a
         *        fresh camera renders every layer, including the editor
         *        one (@ref layer_editor) the debug helpers use; clear that
         *        bit to hide them from a gameplay camera.
         */
        void set_culling_mask(uint32_t mask) noexcept;
        uint32_t culling_mask() const noexcept;

        /**
         * @brief Renders the camera's view into @p target, or into the
         *        window's swapchain for null (the default).
         *
         * The renderer renders every view on a render texture before the
         * views on the swapchain, so a material sampling the texture sees
         * this frame's image; see @ref render_texture for what the texture
         * holds. Non-owning: the texture outlives the camera's use of it.
         */
        void set_target(const render_texture* target) noexcept;
        const render_texture* target() const noexcept;

        /**
         * @brief The rectangle of the target the camera renders into, in
         *        fractions of the target from its bottom-left corner; the
         *        whole target by default. A camera whose rectangle differs
         *        from another's renders alongside it rather than competing
         *        with it: two cameras with a half each split the screen.
         */
        void set_viewport(const viewport_rect& viewport) noexcept;
        const viewport_rect& viewport() const noexcept;

        /**
         * @brief Whether the camera's view composites the game UI; on by
         *        default. Only a view on the swapchain does, drawn in the
         *        view's rectangle.
         */
        void set_draws_ui(bool draws_ui) noexcept;
        bool draws_ui() const noexcept;

    protected:
        mutable core::math::mat4 m_projection;
        mutable bool m_is_projection_matrix_dirty;

    private:
        bool m_enabled{true};
        bool m_main{false};
        int m_priority{0};
        uint32_t m_culling_mask{layer_all};
        const render_texture* m_target{nullptr};
        viewport_rect m_viewport{};
        bool m_draws_ui{true};
    };

    /**
     * @brief World-to-view matrix of a camera whose world matrix is @p world:
     *        its translation is the eye, its +X column the forward and its
     *        +Z column the up (each normalised, so the matrix's scale does
     *        not distort the view). A degenerate (zero-scale) column falls
     *        back to the identity orientation rather than producing NaNs.
     */
    core::math::mat4 view_matrix_from_world(const core::math::mat4& world);
} // namespace rendering_engine
