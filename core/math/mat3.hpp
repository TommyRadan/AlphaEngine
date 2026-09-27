// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/vec3.hpp>

namespace core::math
{
    /** @brief 3x3 float matrix (column-major). Layout-compatible with @c glm::mat3. */
    struct mat3
    {
        // Column-major storage: m[col * 3 + row].
        float m[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

        constexpr mat3() noexcept = default;
        constexpr explicit mat3(float diagonal) noexcept
            : m{diagonal, 0.0f, 0.0f, 0.0f, diagonal, 0.0f, 0.0f, 0.0f, diagonal}
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

    mat3 operator*(const mat3& a, const mat3& b) noexcept;
    vec3 operator*(const mat3& m, const vec3& v) noexcept;
    bool operator==(const mat3& a, const mat3& b) noexcept;
    bool operator!=(const mat3& a, const mat3& b) noexcept;

    mat3 inverse(const mat3& m) noexcept;
    mat3 transpose(const mat3& m) noexcept;
} // namespace core::math
