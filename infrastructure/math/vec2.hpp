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

namespace infrastructure::math
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
} // namespace infrastructure::math
