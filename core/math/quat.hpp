// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/mat4.hpp>
#include <core/math/vec3.hpp>

namespace core::math
{
    /** @brief Float quaternion. Components are exposed in @c (w, x, y, z) order. */
    struct quat
    {
        float w{1.0f};
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};

        constexpr quat() noexcept = default;
        constexpr quat(float in_w, float in_x, float in_y, float in_z) noexcept : w{in_w}, x{in_x}, y{in_y}, z{in_z} {}
    };

    quat normalize(const quat& q) noexcept;
    quat inverse(const quat& q) noexcept;

    /** @brief Hamilton product: composes two rotations (@p a applied after @p b). */
    quat operator*(const quat& a, const quat& b) noexcept;
    /** @brief Rotates @p v by the rotation @p q. */
    vec3 operator*(const quat& q, const vec3& v) noexcept;

    /**
     * @name Component-wise arithmetic
     * Treat the quaternion as a 4-vector, as blending and spline code does
     * (a weighted sum of rotations, a Hermite tangent). The result is in
     * general not a unit quaternion; @ref normalize it before using it as a
     * rotation.
     * @{
     */
    quat operator+(const quat& a, const quat& b) noexcept;
    quat operator-(const quat& a, const quat& b) noexcept;
    quat operator-(const quat& q) noexcept;
    quat operator*(const quat& q, float s) noexcept;
    quat operator*(float s, const quat& q) noexcept;
    /** @} */

    /** @brief Four-component dot product; the cosine of half the angle between two unit rotations. */
    float dot(const quat& a, const quat& b) noexcept;

    /**
     * @brief Normalised linear interpolation from @p a (t = 0) to @p b (t = 1).
     *
     * Takes the shortest path: when the inputs lie in opposite hemispheres
     * (negative dot) @p b is negated first, since @c q and @c -q name the
     * same rotation. Cheaper than @ref slerp and commutative under blending,
     * but its angular speed is not constant across @p t.
     */
    quat nlerp(const quat& a, const quat& b, float t) noexcept;

    /**
     * @brief Spherical linear interpolation from @p a (t = 0) to @p b (t = 1)
     *        at constant angular speed, along the shortest arc.
     *
     * @p b is negated when the inputs lie in opposite hemispheres so the
     * rotation never goes the long way round. When the two are (nearly)
     * parallel the arc is too short for the @c sin divisor to be stable,
     * and the result falls back to @ref nlerp, which is indistinguishable
     * there. Both inputs should be unit quaternions; the result is one.
     */
    quat slerp(const quat& a, const quat& b, float t) noexcept;

    /** @brief Builds a quaternion from intrinsic Tait-Bryan euler angles (radians). */
    quat quat_from_euler(const vec3& euler_radians) noexcept;
    /** @brief Extracts intrinsic Tait-Bryan euler angles (radians) from @p q. */
    vec3 euler_from_quat(const quat& q) noexcept;
    /**
     * @brief Orientation that maps the local X, Y and Z axes onto @p x_axis,
     *        @p y_axis and @p z_axis, which must form a right-handed
     *        orthonormal basis.
     */
    quat quat_from_basis(const vec3& x_axis, const vec3& y_axis, const vec3& z_axis) noexcept;
    /**
     * @brief Orientation whose forward (+X) axis points along @p direction
     *        and whose up (+Z) axis lies in the plane of @p direction and
     *        @p up, in the engine's world convention (see math.hpp).
     *
     * @p direction must be non-zero and not parallel to @p up; callers
     * that cannot guarantee that pass their up through @ref reference_up
     * first. Neither argument needs to be unit length.
     */
    quat quat_look_at(const vec3& direction, const vec3& up) noexcept;
    /** @brief Rotation matrix equivalent to @p q. */
    mat4 to_mat4(const quat& q) noexcept;
} // namespace core::math
