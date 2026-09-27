// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/vec4.hpp>

#include <glm/glm.hpp>

namespace core::math
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
} // namespace core::math
