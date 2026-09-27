// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

namespace core::math
{
    /** @brief 2D float vector. Layout-compatible with @c glm::vec2. */
    struct vec2
    {
        float x{0.0f};
        float y{0.0f};

        constexpr vec2() noexcept = default;
        constexpr explicit vec2(float scalar) noexcept : x{scalar}, y{scalar} {}
        constexpr vec2(float in_x, float in_y) noexcept : x{in_x}, y{in_y} {}

        const float* data() const noexcept
        {
            return &x;
        }
        float* data() noexcept
        {
            return &x;
        }
    };

    vec2 operator+(const vec2& a, const vec2& b) noexcept;
    vec2 operator-(const vec2& a, const vec2& b) noexcept;
    vec2 operator*(const vec2& a, const vec2& b) noexcept;
    vec2 operator/(const vec2& a, const vec2& b) noexcept;
    vec2 operator*(const vec2& v, float s) noexcept;
    vec2 operator*(float s, const vec2& v) noexcept;
    vec2 operator/(const vec2& v, float s) noexcept;
    vec2 operator-(const vec2& v) noexcept;
    bool operator==(const vec2& a, const vec2& b) noexcept;
    bool operator!=(const vec2& a, const vec2& b) noexcept;
    vec2& operator+=(vec2& a, const vec2& b) noexcept;
    vec2& operator-=(vec2& a, const vec2& b) noexcept;
    vec2& operator*=(vec2& a, float s) noexcept;
    vec2& operator/=(vec2& a, float s) noexcept;

    float dot(const vec2& a, const vec2& b) noexcept;
    vec2 normalize(const vec2& v) noexcept;
    float length(const vec2& v) noexcept;
    float distance(const vec2& a, const vec2& b) noexcept;
    vec2 lerp(const vec2& a, const vec2& b, float t) noexcept;
} // namespace core::math
