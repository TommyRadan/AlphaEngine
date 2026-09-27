/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file math.hpp
 * @brief Umbrella include for the engine-owned math types.
 *
 * Each vector, matrix, and quaternion type lives in its own header next to
 * this one. The implementations are in matching @c .cpp files, which are
 * the only translation units that include and name @c glm:: . Including
 * this header pulls every public math type into scope while keeping GLM
 * out of the consumer's preprocessor input.
 *
 * Coordinate conventions
 * ----------------------
 * World and object space are right-handed with **+Z up** and **+X
 * forward**: an identity orientation looks along +X with +Z over its head
 * and, since X x Z = -Y, has -Y on its right (@ref world_forward,
 * @ref world_up, @ref world_right). Every orientation-building routine in
 * the engine — @ref quat_look_at, @c transform::look_at, @c node::look_at,
 * @c camera::look_at — produces a frame in this convention, every
 * up-vector default is @ref world_up, and @ref reference_up is the one
 * place that picks a substitute up when a direction runs along the up
 * axis, so no two call sites can disagree about it.
 *
 * View space is a separate matter: @ref look_at, @ref perspective and
 * @ref ortho keep the OpenGL convention the projection matrices assume
 * (the camera looks down its -Z with +Y up). @c camera::get_view_matrix
 * maps a world-space camera frame onto it, so nothing outside the camera
 * needs to know about view space.
 */

#pragma once

#include <core/math/aabb.hpp>
#include <core/math/frustum.hpp>
#include <core/math/mat3.hpp>
#include <core/math/mat4.hpp>
#include <core/math/quat.hpp>
#include <core/math/ray.hpp>
#include <core/math/sphere.hpp>
#include <core/math/trs.hpp>
#include <core/math/vec2.hpp>
#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>

namespace core::math
{
    float lerp(float a, float b, float t) noexcept;

    /** @brief The direction an identity orientation faces: +X. */
    inline constexpr vec3 world_forward{1.0f, 0.0f, 0.0f};

    /** @brief The engine's up axis: +Z. */
    inline constexpr vec3 world_up{0.0f, 0.0f, 1.0f};

    /**
     * @brief The right-hand side of an identity orientation: -Y, so that
     *        @c cross(world_forward, world_up) == world_right.
     */
    inline constexpr vec3 world_right{0.0f, -1.0f, 0.0f};

    /**
     * @brief The up vector to build a basis around @p direction with.
     *
     * Returns @p up unless it is unusable as a reference for @p direction:
     * (nearly) parallel to it — looking straight up or down the up axis —
     * or zero. Then it falls back to the engine forward (+X), or to +Y when
     * @p direction itself runs (nearly) along +X, so @c cross(direction,
     * result) is never zero and a look-at frame is always well-defined. A
     * zero @p direction returns @p up itself (or @ref world_up when that is
     * zero too).
     */
    vec3 reference_up(const vec3& direction, const vec3& up = world_up) noexcept;
} // namespace core::math
