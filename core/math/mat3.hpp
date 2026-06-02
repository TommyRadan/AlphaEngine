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
