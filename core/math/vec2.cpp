// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/vec2.hpp>

#include <glm/glm.hpp>

namespace core::math
{
    vec2 operator+(const vec2& a, const vec2& b) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} + glm::vec2{b.x, b.y};
        return vec2{result.x, result.y};
    }
    vec2 operator-(const vec2& a, const vec2& b) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} - glm::vec2{b.x, b.y};
        return vec2{result.x, result.y};
    }
    vec2 operator*(const vec2& a, const vec2& b) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} * glm::vec2{b.x, b.y};
        return vec2{result.x, result.y};
    }
    vec2 operator/(const vec2& a, const vec2& b) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} / glm::vec2{b.x, b.y};
        return vec2{result.x, result.y};
    }
    vec2 operator*(const vec2& v, float s) noexcept
    {
        glm::vec2 result = glm::vec2{v.x, v.y} * s;
        return vec2{result.x, result.y};
    }
    vec2 operator*(float s, const vec2& v) noexcept
    {
        glm::vec2 result = s * glm::vec2{v.x, v.y};
        return vec2{result.x, result.y};
    }
    vec2 operator/(const vec2& v, float s) noexcept
    {
        glm::vec2 result = glm::vec2{v.x, v.y} / s;
        return vec2{result.x, result.y};
    }
    vec2 operator-(const vec2& v) noexcept
    {
        glm::vec2 result = -glm::vec2{v.x, v.y};
        return vec2{result.x, result.y};
    }
    bool operator==(const vec2& a, const vec2& b) noexcept
    {
        return glm::vec2{a.x, a.y} == glm::vec2{b.x, b.y};
    }
    bool operator!=(const vec2& a, const vec2& b) noexcept
    {
        return glm::vec2{a.x, a.y} != glm::vec2{b.x, b.y};
    }
    vec2& operator+=(vec2& a, const vec2& b) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} + glm::vec2{b.x, b.y};
        a.x = result.x;
        a.y = result.y;
        return a;
    }
    vec2& operator-=(vec2& a, const vec2& b) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} - glm::vec2{b.x, b.y};
        a.x = result.x;
        a.y = result.y;
        return a;
    }
    vec2& operator*=(vec2& a, float s) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} * s;
        a.x = result.x;
        a.y = result.y;
        return a;
    }
    vec2& operator/=(vec2& a, float s) noexcept
    {
        glm::vec2 result = glm::vec2{a.x, a.y} / s;
        a.x = result.x;
        a.y = result.y;
        return a;
    }

    float dot(const vec2& a, const vec2& b) noexcept
    {
        return glm::dot(glm::vec2{a.x, a.y}, glm::vec2{b.x, b.y});
    }

    vec2 normalize(const vec2& v) noexcept
    {
        glm::vec2 result = glm::normalize(glm::vec2{v.x, v.y});
        return vec2{result.x, result.y};
    }

    float length(const vec2& v) noexcept
    {
        return glm::length(glm::vec2{v.x, v.y});
    }

    float distance(const vec2& a, const vec2& b) noexcept
    {
        return glm::distance(glm::vec2{a.x, a.y}, glm::vec2{b.x, b.y});
    }

    vec2 lerp(const vec2& a, const vec2& b, float t) noexcept
    {
        glm::vec2 result = glm::mix(glm::vec2{a.x, a.y}, glm::vec2{b.x, b.y}, t);
        return vec2{result.x, result.y};
    }
} // namespace core::math
