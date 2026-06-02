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

#include <core/math/vec3.hpp>

#include <glm/glm.hpp>

namespace core::math
{
    vec3 operator+(const vec3& a, const vec3& b) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} + glm::vec3{b.x, b.y, b.z};
        return vec3{result.x, result.y, result.z};
    }
    vec3 operator-(const vec3& a, const vec3& b) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} - glm::vec3{b.x, b.y, b.z};
        return vec3{result.x, result.y, result.z};
    }
    vec3 operator*(const vec3& a, const vec3& b) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} * glm::vec3{b.x, b.y, b.z};
        return vec3{result.x, result.y, result.z};
    }
    vec3 operator/(const vec3& a, const vec3& b) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} / glm::vec3{b.x, b.y, b.z};
        return vec3{result.x, result.y, result.z};
    }
    vec3 operator*(const vec3& v, float s) noexcept
    {
        glm::vec3 result = glm::vec3{v.x, v.y, v.z} * s;
        return vec3{result.x, result.y, result.z};
    }
    vec3 operator*(float s, const vec3& v) noexcept
    {
        glm::vec3 result = s * glm::vec3{v.x, v.y, v.z};
        return vec3{result.x, result.y, result.z};
    }
    vec3 operator/(const vec3& v, float s) noexcept
    {
        glm::vec3 result = glm::vec3{v.x, v.y, v.z} / s;
        return vec3{result.x, result.y, result.z};
    }
    vec3 operator-(const vec3& v) noexcept
    {
        glm::vec3 result = -glm::vec3{v.x, v.y, v.z};
        return vec3{result.x, result.y, result.z};
    }
    bool operator==(const vec3& a, const vec3& b) noexcept
    {
        return glm::vec3{a.x, a.y, a.z} == glm::vec3{b.x, b.y, b.z};
    }
    bool operator!=(const vec3& a, const vec3& b) noexcept
    {
        return glm::vec3{a.x, a.y, a.z} != glm::vec3{b.x, b.y, b.z};
    }
    vec3& operator+=(vec3& a, const vec3& b) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} + glm::vec3{b.x, b.y, b.z};
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        return a;
    }
    vec3& operator-=(vec3& a, const vec3& b) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} - glm::vec3{b.x, b.y, b.z};
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        return a;
    }
    vec3& operator*=(vec3& a, float s) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} * s;
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        return a;
    }
    vec3& operator/=(vec3& a, float s) noexcept
    {
        glm::vec3 result = glm::vec3{a.x, a.y, a.z} / s;
        a.x = result.x;
        a.y = result.y;
        a.z = result.z;
        return a;
    }

    float dot(const vec3& a, const vec3& b) noexcept
    {
        return glm::dot(glm::vec3{a.x, a.y, a.z}, glm::vec3{b.x, b.y, b.z});
    }

    vec3 cross(const vec3& a, const vec3& b) noexcept
    {
        glm::vec3 result = glm::cross(glm::vec3{a.x, a.y, a.z}, glm::vec3{b.x, b.y, b.z});
        return vec3{result.x, result.y, result.z};
    }

    vec3 normalize(const vec3& v) noexcept
    {
        glm::vec3 result = glm::normalize(glm::vec3{v.x, v.y, v.z});
        return vec3{result.x, result.y, result.z};
    }

    float length(const vec3& v) noexcept
    {
        return glm::length(glm::vec3{v.x, v.y, v.z});
    }

    float distance(const vec3& a, const vec3& b) noexcept
    {
        return glm::distance(glm::vec3{a.x, a.y, a.z}, glm::vec3{b.x, b.y, b.z});
    }

    vec3 lerp(const vec3& a, const vec3& b, float t) noexcept
    {
        glm::vec3 result = glm::mix(glm::vec3{a.x, a.y, a.z}, glm::vec3{b.x, b.y, b.z}, t);
        return vec3{result.x, result.y, result.z};
    }
} // namespace core::math
