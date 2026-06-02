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

#include <core/math/vec3.hpp>

namespace core::math
{
    /** @brief 4D float vector. Layout-compatible with @c glm::vec4. */
    struct vec4
    {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float w{0.0f};

        constexpr vec4() noexcept = default;
        constexpr explicit vec4(float scalar) noexcept : x{scalar}, y{scalar}, z{scalar}, w{scalar} {}
        constexpr vec4(float in_x, float in_y, float in_z, float in_w) noexcept : x{in_x}, y{in_y}, z{in_z}, w{in_w} {}
        constexpr vec4(const vec3& xyz, float in_w) noexcept : x{xyz.x}, y{xyz.y}, z{xyz.z}, w{in_w} {}

        const float* data() const noexcept
        {
            return &x;
        }
        float* data() noexcept
        {
            return &x;
        }
    };

    vec4 operator+(const vec4& a, const vec4& b) noexcept;
    vec4 operator-(const vec4& a, const vec4& b) noexcept;
    vec4 operator*(const vec4& a, const vec4& b) noexcept;
    vec4 operator/(const vec4& a, const vec4& b) noexcept;
    vec4 operator*(const vec4& v, float s) noexcept;
    vec4 operator*(float s, const vec4& v) noexcept;
    vec4 operator/(const vec4& v, float s) noexcept;
    vec4 operator-(const vec4& v) noexcept;
    bool operator==(const vec4& a, const vec4& b) noexcept;
    bool operator!=(const vec4& a, const vec4& b) noexcept;
    vec4& operator+=(vec4& a, const vec4& b) noexcept;
    vec4& operator-=(vec4& a, const vec4& b) noexcept;
    vec4& operator*=(vec4& a, float s) noexcept;
    vec4& operator/=(vec4& a, float s) noexcept;

    float dot(const vec4& a, const vec4& b) noexcept;
    vec4 normalize(const vec4& v) noexcept;
    float length(const vec4& v) noexcept;
    float distance(const vec4& a, const vec4& b) noexcept;
    vec4 lerp(const vec4& a, const vec4& b, float t) noexcept;
} // namespace core::math
