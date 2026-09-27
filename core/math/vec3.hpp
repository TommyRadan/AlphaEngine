// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

namespace core::math
{
    /** @brief 3D float vector. Layout-compatible with @c glm::vec3. */
    struct vec3
    {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};

        constexpr vec3() noexcept = default;
        constexpr explicit vec3(float scalar) noexcept : x{scalar}, y{scalar}, z{scalar} {}
        constexpr vec3(float in_x, float in_y, float in_z) noexcept : x{in_x}, y{in_y}, z{in_z} {}

        const float* data() const noexcept
        {
            return &x;
        }
        float* data() noexcept
        {
            return &x;
        }
    };

    vec3 operator+(const vec3& a, const vec3& b) noexcept;
    vec3 operator-(const vec3& a, const vec3& b) noexcept;
    vec3 operator*(const vec3& a, const vec3& b) noexcept;
    vec3 operator/(const vec3& a, const vec3& b) noexcept;
    vec3 operator*(const vec3& v, float s) noexcept;
    vec3 operator*(float s, const vec3& v) noexcept;
    vec3 operator/(const vec3& v, float s) noexcept;
    vec3 operator-(const vec3& v) noexcept;
    bool operator==(const vec3& a, const vec3& b) noexcept;
    bool operator!=(const vec3& a, const vec3& b) noexcept;
    vec3& operator+=(vec3& a, const vec3& b) noexcept;
    vec3& operator-=(vec3& a, const vec3& b) noexcept;
    vec3& operator*=(vec3& a, float s) noexcept;
    vec3& operator/=(vec3& a, float s) noexcept;

    float dot(const vec3& a, const vec3& b) noexcept;
    vec3 cross(const vec3& a, const vec3& b) noexcept;
    vec3 normalize(const vec3& v) noexcept;
    float length(const vec3& v) noexcept;
    float distance(const vec3& a, const vec3& b) noexcept;
    vec3 lerp(const vec3& a, const vec3& b, float t) noexcept;
} // namespace core::math
