// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file camera_registry.hpp
 * @brief The renderer's list of candidate cameras and the arbitration
 *        that picks the one a frame renders with.
 */

#pragma once

#include <vector>

namespace rendering_engine
{
    struct camera;

    /**
     * @brief The cameras attached right now, in attach order.
     *
     * Non-owning back-pointers: a camera joins through @ref camera::attach
     * (re-attaching moves it to the back) and leaves through
     * @ref camera::detach or its destructor. Like the light registry this
     * is process-wide and main-thread-only, alive before any camera is
     * constructed — game modules create cameras from static-init hooks.
     */
    const std::vector<camera*>& registered_cameras();

    /**
     * @brief The camera a frame renders with, or @c nullptr when no
     *        attached camera is enabled.
     *
     * Arbitration: the highest @ref camera::get_priority among the attached,
     * enabled cameras wins; a priority tie goes to a camera tagged
     * @ref camera::is_main over one that is not, and between equals to the
     * most recently attached. @ref renderer::render evaluates this once per
     * frame into @c frame_context::active_camera, so destroying or disabling
     * the winner promotes the runner-up on the next frame with no
     * bookkeeping by the owner.
     */
    camera* active_camera();

    /**
     * @brief The attached, enabled camera tagged main with the highest
     *        priority (the most recently attached on a tie), or @c nullptr.
     *
     * A way for game code to find "the player's camera" while a
     * higher-priority camera (a cutscene, a debug fly-cam) is rendering.
     */
    camera* main_camera();

    /**
     * @brief Records the drawable's width / height and forwards it to every
     *        attached camera's @ref camera::set_aspect_ratio; a camera
     *        attached later receives it on attach.
     *
     * The renderer calls this at init and from @ref renderer::on_resize, so an
     * attached camera's projection always matches the drawable. A value of
     * zero or less clears it (nothing is forwarded, later attaches leave the
     * camera's aspect alone).
     */
    void set_drawable_aspect(float aspect_ratio);

    /** @brief The last value given to @ref set_drawable_aspect, or 0 when none is known. */
    float drawable_aspect();

    // The registration primitives behind camera::attach / camera::detach /
    // camera::is_attached; call those instead. register_camera appends the
    // camera (moving it to the back if it is already listed) and hands it the
    // current drawable aspect; unregister_camera is a no-op for an unlisted
    // camera.
    void register_camera(camera& cam);
    void unregister_camera(camera& cam);
    bool is_registered(const camera& cam);
} // namespace rendering_engine
