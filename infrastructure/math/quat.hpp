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

#pragma once

#include <infrastructure/math/mat4.hpp>
#include <infrastructure/math/vec3.hpp>

namespace infrastructure::math
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

    /** @brief Builds a quaternion from intrinsic Tait-Bryan euler angles (radians). */
    quat quat_from_euler(const vec3& euler_radians) noexcept;
    /** @brief Extracts intrinsic Tait-Bryan euler angles (radians) from @p q. */
    vec3 euler_from_quat(const quat& q) noexcept;
    /** @brief Orientation whose forward (-Z) axis points along @p direction, with @p up as the reference up. */
    quat quat_look_at(const vec3& direction, const vec3& up) noexcept;
    /** @brief Rotation matrix equivalent to @p q. */
    mat4 to_mat4(const quat& q) noexcept;
} // namespace infrastructure::math
