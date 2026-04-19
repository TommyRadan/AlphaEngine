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
 * @brief Engine-owned math types that wrap GLM.
 *
 * The wrapper exists so that engine and game code can depend on a stable,
 * engine-owned API rather than GLM directly. The wrapped types hold their
 * underlying @c glm::* value as a public member named @c value and provide
 * component access through an anonymous union. Memory layout is identical
 * to the corresponding GLM type, so uniform uploads via @c data() do not
 * need to change.
 *
 * GLM is included here publicly: this is an accepted trade for inlining —
 * the only rule is that @c glm:: must not appear outside
 * @c infrastructure/math/.
 */

#pragma once

#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

namespace infrastructure::math
{
    // -------------------------------------------------------------------------
    // Vector types
    // -------------------------------------------------------------------------

    /** @brief 2D float vector. Layout-compatible with @c glm::vec2. */
    struct vec2
    {
        union
        {
            glm::vec2 value;
            struct
            {
                float x, y;
            };
        };

        constexpr vec2() noexcept : value{0.0f, 0.0f} {}
        constexpr explicit vec2(float scalar) noexcept : value{scalar, scalar} {}
        constexpr vec2(float in_x, float in_y) noexcept : value{in_x, in_y} {}
        constexpr vec2(const glm::vec2& v) noexcept : value{v} {}

        constexpr vec2(const vec2& other) noexcept : value{other.value} {}
        constexpr vec2& operator=(const vec2& other) noexcept
        {
            value = other.value;
            return *this;
        }

        constexpr operator glm::vec2&() noexcept
        {
            return value;
        }
        constexpr operator const glm::vec2&() const noexcept
        {
            return value;
        }

        const float* data() const noexcept
        {
            return &value.x;
        }
        float* data() noexcept
        {
            return &value.x;
        }
    };

    /** @brief 3D float vector. Layout-compatible with @c glm::vec3. */
    struct vec3
    {
        union
        {
            glm::vec3 value;
            struct
            {
                float x, y, z;
            };
        };

        constexpr vec3() noexcept : value{0.0f, 0.0f, 0.0f} {}
        constexpr explicit vec3(float scalar) noexcept : value{scalar, scalar, scalar} {}
        constexpr vec3(float in_x, float in_y, float in_z) noexcept : value{in_x, in_y, in_z} {}
        constexpr vec3(const glm::vec3& v) noexcept : value{v} {}

        constexpr vec3(const vec3& other) noexcept : value{other.value} {}
        constexpr vec3& operator=(const vec3& other) noexcept
        {
            value = other.value;
            return *this;
        }

        constexpr operator glm::vec3&() noexcept
        {
            return value;
        }
        constexpr operator const glm::vec3&() const noexcept
        {
            return value;
        }

        const float* data() const noexcept
        {
            return &value.x;
        }
        float* data() noexcept
        {
            return &value.x;
        }
    };

    /** @brief 4D float vector. Layout-compatible with @c glm::vec4. */
    struct vec4
    {
        union
        {
            glm::vec4 value;
            struct
            {
                float x, y, z, w;
            };
        };

        constexpr vec4() noexcept : value{0.0f, 0.0f, 0.0f, 0.0f} {}
        constexpr explicit vec4(float scalar) noexcept : value{scalar, scalar, scalar, scalar} {}
        constexpr vec4(float in_x, float in_y, float in_z, float in_w) noexcept : value{in_x, in_y, in_z, in_w} {}
        constexpr vec4(const vec3& xyz, float in_w) noexcept : value{xyz.value, in_w} {}
        constexpr vec4(const glm::vec4& v) noexcept : value{v} {}

        constexpr vec4(const vec4& other) noexcept : value{other.value} {}
        constexpr vec4& operator=(const vec4& other) noexcept
        {
            value = other.value;
            return *this;
        }

        constexpr operator glm::vec4&() noexcept
        {
            return value;
        }
        constexpr operator const glm::vec4&() const noexcept
        {
            return value;
        }

        const float* data() const noexcept
        {
            return &value.x;
        }
        float* data() noexcept
        {
            return &value.x;
        }
    };

    // -------------------------------------------------------------------------
    // Matrix types
    // -------------------------------------------------------------------------

    /** @brief 3x3 float matrix (column-major). Layout-compatible with @c glm::mat3. */
    struct mat3
    {
        glm::mat3 value;

        mat3() noexcept : value{1.0f} {}
        explicit mat3(float diagonal) noexcept : value{diagonal} {}
        mat3(const glm::mat3& m) noexcept : value{m} {}

        operator glm::mat3&() noexcept
        {
            return value;
        }
        operator const glm::mat3&() const noexcept
        {
            return value;
        }

        const float* data() const noexcept
        {
            return &value[0][0];
        }
        float* data() noexcept
        {
            return &value[0][0];
        }
    };

    /** @brief 4x4 float matrix (column-major). Layout-compatible with @c glm::mat4. */
    struct mat4
    {
        glm::mat4 value;

        mat4() noexcept : value{1.0f} {}
        explicit mat4(float diagonal) noexcept : value{diagonal} {}
        mat4(const glm::mat4& m) noexcept : value{m} {}

        operator glm::mat4&() noexcept
        {
            return value;
        }
        operator const glm::mat4&() const noexcept
        {
            return value;
        }

        const float* data() const noexcept
        {
            return &value[0][0];
        }
        float* data() noexcept
        {
            return &value[0][0];
        }
    };

    /** @brief Float quaternion. Layout-compatible with @c glm::quat. */
    struct quat
    {
        glm::quat value;

        quat() noexcept : value{1.0f, 0.0f, 0.0f, 0.0f} {}
        quat(float w, float x, float y, float z) noexcept : value{w, x, y, z} {}
        quat(const glm::quat& q) noexcept : value{q} {}

        operator glm::quat&() noexcept
        {
            return value;
        }
        operator const glm::quat&() const noexcept
        {
            return value;
        }
    };

    // -------------------------------------------------------------------------
    // Vector operators
    // -------------------------------------------------------------------------

    inline vec2 operator+(const vec2& a, const vec2& b) noexcept
    {
        return vec2{a.value + b.value};
    }
    inline vec2 operator-(const vec2& a, const vec2& b) noexcept
    {
        return vec2{a.value - b.value};
    }
    inline vec2 operator*(const vec2& a, const vec2& b) noexcept
    {
        return vec2{a.value * b.value};
    }
    inline vec2 operator/(const vec2& a, const vec2& b) noexcept
    {
        return vec2{a.value / b.value};
    }
    inline vec2 operator*(const vec2& v, float s) noexcept
    {
        return vec2{v.value * s};
    }
    inline vec2 operator*(float s, const vec2& v) noexcept
    {
        return vec2{s * v.value};
    }
    inline vec2 operator/(const vec2& v, float s) noexcept
    {
        return vec2{v.value / s};
    }
    inline vec2 operator-(const vec2& v) noexcept
    {
        return vec2{-v.value};
    }
    inline bool operator==(const vec2& a, const vec2& b) noexcept
    {
        return a.value == b.value;
    }
    inline bool operator!=(const vec2& a, const vec2& b) noexcept
    {
        return a.value != b.value;
    }
    inline vec2& operator+=(vec2& a, const vec2& b) noexcept
    {
        a.value += b.value;
        return a;
    }
    inline vec2& operator-=(vec2& a, const vec2& b) noexcept
    {
        a.value -= b.value;
        return a;
    }
    inline vec2& operator*=(vec2& a, float s) noexcept
    {
        a.value *= s;
        return a;
    }
    inline vec2& operator/=(vec2& a, float s) noexcept
    {
        a.value /= s;
        return a;
    }

    inline vec3 operator+(const vec3& a, const vec3& b) noexcept
    {
        return vec3{a.value + b.value};
    }
    inline vec3 operator-(const vec3& a, const vec3& b) noexcept
    {
        return vec3{a.value - b.value};
    }
    inline vec3 operator*(const vec3& a, const vec3& b) noexcept
    {
        return vec3{a.value * b.value};
    }
    inline vec3 operator/(const vec3& a, const vec3& b) noexcept
    {
        return vec3{a.value / b.value};
    }
    inline vec3 operator*(const vec3& v, float s) noexcept
    {
        return vec3{v.value * s};
    }
    inline vec3 operator*(float s, const vec3& v) noexcept
    {
        return vec3{s * v.value};
    }
    inline vec3 operator/(const vec3& v, float s) noexcept
    {
        return vec3{v.value / s};
    }
    inline vec3 operator-(const vec3& v) noexcept
    {
        return vec3{-v.value};
    }
    inline bool operator==(const vec3& a, const vec3& b) noexcept
    {
        return a.value == b.value;
    }
    inline bool operator!=(const vec3& a, const vec3& b) noexcept
    {
        return a.value != b.value;
    }
    inline vec3& operator+=(vec3& a, const vec3& b) noexcept
    {
        a.value += b.value;
        return a;
    }
    inline vec3& operator-=(vec3& a, const vec3& b) noexcept
    {
        a.value -= b.value;
        return a;
    }
    inline vec3& operator*=(vec3& a, float s) noexcept
    {
        a.value *= s;
        return a;
    }
    inline vec3& operator/=(vec3& a, float s) noexcept
    {
        a.value /= s;
        return a;
    }

    inline vec4 operator+(const vec4& a, const vec4& b) noexcept
    {
        return vec4{a.value + b.value};
    }
    inline vec4 operator-(const vec4& a, const vec4& b) noexcept
    {
        return vec4{a.value - b.value};
    }
    inline vec4 operator*(const vec4& a, const vec4& b) noexcept
    {
        return vec4{a.value * b.value};
    }
    inline vec4 operator/(const vec4& a, const vec4& b) noexcept
    {
        return vec4{a.value / b.value};
    }
    inline vec4 operator*(const vec4& v, float s) noexcept
    {
        return vec4{v.value * s};
    }
    inline vec4 operator*(float s, const vec4& v) noexcept
    {
        return vec4{s * v.value};
    }
    inline vec4 operator/(const vec4& v, float s) noexcept
    {
        return vec4{v.value / s};
    }
    inline vec4 operator-(const vec4& v) noexcept
    {
        return vec4{-v.value};
    }
    inline bool operator==(const vec4& a, const vec4& b) noexcept
    {
        return a.value == b.value;
    }
    inline bool operator!=(const vec4& a, const vec4& b) noexcept
    {
        return a.value != b.value;
    }
    inline vec4& operator+=(vec4& a, const vec4& b) noexcept
    {
        a.value += b.value;
        return a;
    }
    inline vec4& operator-=(vec4& a, const vec4& b) noexcept
    {
        a.value -= b.value;
        return a;
    }
    inline vec4& operator*=(vec4& a, float s) noexcept
    {
        a.value *= s;
        return a;
    }
    inline vec4& operator/=(vec4& a, float s) noexcept
    {
        a.value /= s;
        return a;
    }

    // -------------------------------------------------------------------------
    // Matrix operators
    // -------------------------------------------------------------------------

    inline mat3 operator*(const mat3& a, const mat3& b) noexcept
    {
        return mat3{a.value * b.value};
    }
    inline vec3 operator*(const mat3& m, const vec3& v) noexcept
    {
        return vec3{m.value * v.value};
    }
    inline bool operator==(const mat3& a, const mat3& b) noexcept
    {
        return a.value == b.value;
    }
    inline bool operator!=(const mat3& a, const mat3& b) noexcept
    {
        return a.value != b.value;
    }

    inline mat4 operator*(const mat4& a, const mat4& b) noexcept
    {
        return mat4{a.value * b.value};
    }
    inline vec4 operator*(const mat4& m, const vec4& v) noexcept
    {
        return vec4{m.value * v.value};
    }
    inline vec4 operator*(const vec4& v, const mat4& m) noexcept
    {
        return vec4{v.value * m.value};
    }
    inline bool operator==(const mat4& a, const mat4& b) noexcept
    {
        return a.value == b.value;
    }
    inline bool operator!=(const mat4& a, const mat4& b) noexcept
    {
        return a.value != b.value;
    }
    inline mat4& operator*=(mat4& a, const mat4& b) noexcept
    {
        a.value *= b.value;
        return a;
    }

    // -------------------------------------------------------------------------
    // Free functions
    // -------------------------------------------------------------------------

    inline float dot(const vec2& a, const vec2& b) noexcept
    {
        return glm::dot(a.value, b.value);
    }
    inline float dot(const vec3& a, const vec3& b) noexcept
    {
        return glm::dot(a.value, b.value);
    }
    inline float dot(const vec4& a, const vec4& b) noexcept
    {
        return glm::dot(a.value, b.value);
    }

    inline vec3 cross(const vec3& a, const vec3& b) noexcept
    {
        return vec3{glm::cross(a.value, b.value)};
    }

    inline vec2 normalize(const vec2& v) noexcept
    {
        return vec2{glm::normalize(v.value)};
    }
    inline vec3 normalize(const vec3& v) noexcept
    {
        return vec3{glm::normalize(v.value)};
    }
    inline vec4 normalize(const vec4& v) noexcept
    {
        return vec4{glm::normalize(v.value)};
    }
    inline quat normalize(const quat& q) noexcept
    {
        return quat{glm::normalize(q.value)};
    }

    inline float length(const vec2& v) noexcept
    {
        return glm::length(v.value);
    }
    inline float length(const vec3& v) noexcept
    {
        return glm::length(v.value);
    }
    inline float length(const vec4& v) noexcept
    {
        return glm::length(v.value);
    }

    inline float distance(const vec2& a, const vec2& b) noexcept
    {
        return glm::distance(a.value, b.value);
    }
    inline float distance(const vec3& a, const vec3& b) noexcept
    {
        return glm::distance(a.value, b.value);
    }
    inline float distance(const vec4& a, const vec4& b) noexcept
    {
        return glm::distance(a.value, b.value);
    }

    inline float lerp(float a, float b, float t) noexcept
    {
        return glm::mix(a, b, t);
    }
    inline vec2 lerp(const vec2& a, const vec2& b, float t) noexcept
    {
        return vec2{glm::mix(a.value, b.value, t)};
    }
    inline vec3 lerp(const vec3& a, const vec3& b, float t) noexcept
    {
        return vec3{glm::mix(a.value, b.value, t)};
    }
    inline vec4 lerp(const vec4& a, const vec4& b, float t) noexcept
    {
        return vec4{glm::mix(a.value, b.value, t)};
    }

    inline mat4 look_at(const vec3& eye, const vec3& center, const vec3& up) noexcept
    {
        return mat4{glm::lookAt(eye.value, center.value, up.value)};
    }

    inline mat4 perspective(float fov_y, float aspect, float near_z, float far_z) noexcept
    {
        return mat4{glm::perspective(fov_y, aspect, near_z, far_z)};
    }

    inline mat4 ortho(float left, float right, float bottom, float top, float near_z, float far_z) noexcept
    {
        return mat4{glm::ortho(left, right, bottom, top, near_z, far_z)};
    }

    inline mat4 translate(const vec3& v) noexcept
    {
        return mat4{glm::translate(v.value)};
    }

    inline mat4 translate(const mat4& m, const vec3& v) noexcept
    {
        return mat4{glm::translate(m.value, v.value)};
    }

    inline mat4 rotate(float angle, const vec3& axis) noexcept
    {
        return mat4{glm::rotate(angle, axis.value)};
    }

    inline mat4 rotate(const mat4& m, float angle, const vec3& axis) noexcept
    {
        return mat4{glm::rotate(m.value, angle, axis.value)};
    }

    inline mat4 scale(const vec3& v) noexcept
    {
        return mat4{glm::scale(v.value)};
    }

    inline mat4 scale(const mat4& m, const vec3& v) noexcept
    {
        return mat4{glm::scale(m.value, v.value)};
    }

    inline mat3 inverse(const mat3& m) noexcept
    {
        return mat3{glm::inverse(m.value)};
    }
    inline mat4 inverse(const mat4& m) noexcept
    {
        return mat4{glm::inverse(m.value)};
    }
    inline quat inverse(const quat& q) noexcept
    {
        return quat{glm::inverse(q.value)};
    }

    inline mat3 transpose(const mat3& m) noexcept
    {
        return mat3{glm::transpose(m.value)};
    }
    inline mat4 transpose(const mat4& m) noexcept
    {
        return mat4{glm::transpose(m.value)};
    }
} // namespace infrastructure::math
