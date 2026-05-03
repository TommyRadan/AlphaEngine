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
} // namespace infrastructure::math
