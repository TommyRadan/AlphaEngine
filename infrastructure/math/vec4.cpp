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

#include <infrastructure/math/vec4.hpp>

#include <glm/glm.hpp>

namespace infrastructure::math
{
    vec4 operator+(const vec4& a, const vec4& b) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} + glm::vec4{b.x, b.y, b.z, b.w};
        return vec4{result.x, result.y, result.z, result.w};
    }
    vec4 operator-(const vec4& a, const vec4& b) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} - glm::vec4{b.x, b.y, b.z, b.w};
        return vec4{result.x, result.y, result.z, result.w};
    }
    vec4 operator*(const vec4& a, const vec4& b) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} * glm::vec4{b.x, b.y, b.z, b.w};
        return vec4{result.x, result.y, result.z, result.w};
    }
    vec4 operator/(const vec4& a, const vec4& b) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} / glm::vec4{b.x, b.y, b.z, b.w};
        return vec4{result.x, result.y, result.z, result.w};
    }
    vec4 operator*(const vec4& v, float s) noexcept
    {
        glm::vec4 result = glm::vec4{v.x, v.y, v.z, v.w} * s;
        return vec4{result.x, result.y, result.z, result.w};
    }
    vec4 operator*(float s, const vec4& v) noexcept
    {
        glm::vec4 result = s * glm::vec4{v.x, v.y, v.z, v.w};
        return vec4{result.x, result.y, result.z, result.w};
    }
    vec4 operator/(const vec4& v, float s) noexcept
    {
        glm::vec4 result = glm::vec4{v.x, v.y, v.z, v.w} / s;
        return vec4{result.x, result.y, result.z, result.w};
    }
    vec4 operator-(const vec4& v) noexcept
    {
        glm::vec4 result = -glm::vec4{v.x, v.y, v.z, v.w};
        return vec4{result.x, result.y, result.z, result.w};
    }
    bool operator==(const vec4& a, const vec4& b) noexcept
    {
        return glm::vec4{a.x, a.y, a.z, a.w} == glm::vec4{b.x, b.y, b.z, b.w};
    }
    bool operator!=(const vec4& a, const vec4& b) noexcept
    {
        return glm::vec4{a.x, a.y, a.z, a.w} != glm::vec4{b.x, b.y, b.z, b.w};
    }
    vec4& operator+=(vec4& a, const vec4& b) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} + glm::vec4{b.x, b.y, b.z, b.w};
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        a.w = result.w;
        return a;
    }
    vec4& operator-=(vec4& a, const vec4& b) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} - glm::vec4{b.x, b.y, b.z, b.w};
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        a.w = result.w;
        return a;
    }
    vec4& operator*=(vec4& a, float s) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} * s;
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        a.w = result.w;
        return a;
    }
    vec4& operator/=(vec4& a, float s) noexcept
    {
        glm::vec4 result = glm::vec4{a.x, a.y, a.z, a.w} / s;
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        a.w = result.w;
        return a;
    }

    float dot(const vec4& a, const vec4& b) noexcept
    {
        return glm::dot(glm::vec4{a.x, a.y, a.z, a.w}, glm::vec4{b.x, b.y, b.z, b.w});
    }

    vec4 normalize(const vec4& v) noexcept
    {
        glm::vec4 result = glm::normalize(glm::vec4{v.x, v.y, v.z, v.w});
        return vec4{result.x, result.y, result.z, result.w};
    }

    float length(const vec4& v) noexcept
    {
        return glm::length(glm::vec4{v.x, v.y, v.z, v.w});
    }

    float distance(const vec4& a, const vec4& b) noexcept
    {
        return glm::distance(glm::vec4{a.x, a.y, a.z, a.w}, glm::vec4{b.x, b.y, b.z, b.w});
    }

    vec4 lerp(const vec4& a, const vec4& b, float t) noexcept
    {
        glm::vec4 result = glm::mix(glm::vec4{a.x, a.y, a.z, a.w}, glm::vec4{b.x, b.y, b.z, b.w}, t);
        return vec4{result.x, result.y, result.z, result.w};
    }
} // namespace infrastructure::math
