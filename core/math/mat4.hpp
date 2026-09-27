// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>

namespace core::math
{
    /** @brief 4x4 float matrix (column-major). Layout-compatible with @c glm::mat4. */
    struct mat4
    {
        // Column-major storage: m[col * 4 + row].
        float m[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

        constexpr mat4() noexcept = default;
        constexpr explicit mat4(float diagonal) noexcept
            : m{diagonal,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                diagonal,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                diagonal,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                diagonal}
        {
        }

        const float* data() const noexcept
        {
            return m;
        }
        float* data() noexcept
        {
            return m;
        }
    };

    mat4 operator*(const mat4& a, const mat4& b) noexcept;
    vec4 operator*(const mat4& m, const vec4& v) noexcept;
    vec4 operator*(const vec4& v, const mat4& m) noexcept;
    bool operator==(const mat4& a, const mat4& b) noexcept;
    bool operator!=(const mat4& a, const mat4& b) noexcept;
    mat4& operator*=(mat4& a, const mat4& b) noexcept;

    mat4 look_at(const vec3& eye, const vec3& center, const vec3& up) noexcept;
    mat4 perspective(float fov_y, float aspect, float near_z, float far_z) noexcept;
    mat4 ortho(float left, float right, float bottom, float top, float near_z, float far_z) noexcept;

    mat4 translate(const vec3& v) noexcept;
    mat4 translate(const mat4& m, const vec3& v) noexcept;
    mat4 rotate(float angle, const vec3& axis) noexcept;
    mat4 rotate(const mat4& m, float angle, const vec3& axis) noexcept;
    mat4 scale(const vec3& v) noexcept;
    mat4 scale(const mat4& m, const vec3& v) noexcept;

    mat4 inverse(const mat4& m) noexcept;
    mat4 transpose(const mat4& m) noexcept;
} // namespace core::math
