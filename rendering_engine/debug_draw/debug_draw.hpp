// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file debug_draw.hpp
 * @brief Immediate-mode debug drawing: lines, boxes, spheres, axes, grids,
 *        frusta, arrows and text, drawn in the frame they are recorded in.
 */

#pragma once

#include <string_view>

#include <core/math/aabb.hpp>
#include <core/math/mat4.hpp>
#include <core/math/vec3.hpp>
#include <rendering_engine/render_world.hpp>

/**
 * @brief Immediate-mode debug drawing, in the style of Unreal's
 *        @c DrawDebugLine family and Unity's Gizmos.
 *
 * Call a function every frame something should be drawn; there is no
 * object to create, keep or destroy. Each records into the debug-draw list
 * of the @ref render_world it is handed (@c scene::world() for world code,
 * @c renderer::world() for tools), which the renderer turns into its debug
 * line proxies between frames. Every function takes a colour — linear RGB,
 * each channel in [0, 1] — a @p duration in seconds of world time (0, the
 * default, draws for the one frame the call is made in; see
 * @ref draw_list::advance) and @p depth_test: off, the default, the
 * geometry draws on top of everything in the debug pass; on, it is drawn
 * with the scene and hidden behind whatever is nearer. All geometry is
 * one-pixel lines in world space.
 *
 * Debug builds only: in any other build @ref enabled is false and every
 * function compiles to nothing, so calls cost nothing there.
 */
namespace rendering_engine::debug_draw
{
    // Whether the functions below record anything: in Debug builds only.
#if _DEBUG
    inline constexpr bool enabled = true;
#else
    inline constexpr bool enabled = false;
#endif

    namespace detail
    {
        void add_box(draw_list& list,
                     const core::math::aabb& bounds,
                     const core::math::mat4& transform,
                     const core::math::vec3& color,
                     float duration,
                     bool depth_test);
        void add_sphere(draw_list& list,
                        const core::math::vec3& center,
                        float radius,
                        const core::math::vec3& color,
                        float duration,
                        bool depth_test);
        void add_axes(draw_list& list, const core::math::mat4& transform, float size, float duration, bool depth_test);
        void add_grid(draw_list& list,
                      const core::math::mat4& transform,
                      float size,
                      int divisions,
                      const core::math::vec3& color,
                      const core::math::vec3& center_color,
                      float duration,
                      bool depth_test);
        void add_frustum(draw_list& list,
                         const core::math::mat4& view_projection,
                         const core::math::vec3& color,
                         float duration,
                         bool depth_test);
        void add_arrow(draw_list& list,
                       const core::math::vec3& from,
                       const core::math::vec3& to,
                       const core::math::vec3& color,
                       float duration,
                       bool depth_test);
    } // namespace detail

    /** @brief The segment from @p from to @p to. */
    inline void line(render_world& world,
                     const core::math::vec3& from,
                     const core::math::vec3& to,
                     const core::math::vec3& color,
                     float duration = 0.0f,
                     bool depth_test = false)
    {
        if constexpr (enabled)
        {
            world.debug_draw_list().add_line(from, to, color, duration, depth_test);
        }
    }

    /** @brief The twelve edges of the axis-aligned box @p bounds. */
    inline void box(render_world& world,
                    const core::math::aabb& bounds,
                    const core::math::vec3& color,
                    float duration = 0.0f,
                    bool depth_test = false)
    {
        if constexpr (enabled)
        {
            detail::add_box(world.debug_draw_list(), bounds, core::math::mat4{}, color, duration, depth_test);
        }
    }

    /**
     * @brief The twelve edges of an oriented box: @p bounds in the space
     *        @p transform places in the world (the object-space bounds of
     *        what a node draws and the node's world matrix, say).
     */
    inline void box(render_world& world,
                    const core::math::aabb& bounds,
                    const core::math::mat4& transform,
                    const core::math::vec3& color,
                    float duration = 0.0f,
                    bool depth_test = false)
    {
        if constexpr (enabled)
        {
            detail::add_box(world.debug_draw_list(), bounds, transform, color, duration, depth_test);
        }
    }

    /**
     * @brief A sphere of @p radius around @p center, as its three great
     *        circles in the XY, XZ and YZ planes.
     */
    inline void sphere(render_world& world,
                       const core::math::vec3& center,
                       float radius,
                       const core::math::vec3& color,
                       float duration = 0.0f,
                       bool depth_test = false)
    {
        if constexpr (enabled)
        {
            detail::add_sphere(world.debug_draw_list(), center, radius, color, duration, depth_test);
        }
    }

    /**
     * @brief The three axes of @p transform's space, @p size long from its
     *        origin: +X red, +Y green, +Z blue (the colours are the axes').
     */
    inline void axes(render_world& world,
                     const core::math::mat4& transform,
                     float size = 1.0f,
                     float duration = 0.0f,
                     bool depth_test = false)
    {
        if constexpr (enabled)
        {
            detail::add_axes(world.debug_draw_list(), transform, size, duration, depth_test);
        }
    }

    /**
     * @brief A square grid @p size across on the XY plane of @p transform's
     *        space (the ground, with the engine's +Z up), centred on its
     *        origin, with @p divisions cells per side (at least 1).
     *
     * The line through the origin on each axis is drawn in
     * @p center_color and every other line in @p color; with an odd
     * division count, which puts the origin in the middle of a cell, the
     * centre lines are added to the evenly spaced ones.
     */
    inline void grid(render_world& world,
                     const core::math::mat4& transform,
                     float size,
                     int divisions,
                     const core::math::vec3& color,
                     const core::math::vec3& center_color,
                     float duration = 0.0f,
                     bool depth_test = false)
    {
        if constexpr (enabled)
        {
            detail::add_grid(
                world.debug_draw_list(), transform, size, divisions, color, center_color, duration, depth_test);
        }
    }

    /**
     * @brief The twelve edges of the view volume @p view_projection maps to
     *        clip space (a camera's projection times its view): its eight
     *        corners unprojected into the world, with the near plane at clip
     *        depth 0 and the far plane at 1.
     */
    inline void frustum(render_world& world,
                        const core::math::mat4& view_projection,
                        const core::math::vec3& color,
                        float duration = 0.0f,
                        bool depth_test = false)
    {
        if constexpr (enabled)
        {
            detail::add_frustum(world.debug_draw_list(), view_projection, color, duration, depth_test);
        }
    }

    /**
     * @brief An arrow from @p from to @p to: the shaft and a head of four
     *        barbs a fifth of the arrow long at @p to.
     */
    inline void arrow(render_world& world,
                      const core::math::vec3& from,
                      const core::math::vec3& to,
                      const core::math::vec3& color,
                      float duration = 0.0f,
                      bool depth_test = false)
    {
        if constexpr (enabled)
        {
            detail::add_arrow(world.debug_draw_list(), from, to, color, duration, depth_test);
        }
    }

    /**
     * @brief @p label as screen-space text anchored at the world-space
     *        @p position, always on top.
     *
     * The editor's overlay draws it, projected through the active camera;
     * a label behind the camera is not drawn. It takes no @p depth_test:
     * text is never hidden by the scene.
     */
    inline void text(render_world& world,
                     const core::math::vec3& position,
                     std::string_view label,
                     const core::math::vec3& color,
                     float duration = 0.0f)
    {
        if constexpr (enabled)
        {
            world.debug_draw_list().add_text(position, label, color, duration);
        }
    }
} // namespace rendering_engine::debug_draw
