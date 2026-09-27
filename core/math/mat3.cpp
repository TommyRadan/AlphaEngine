// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/mat3.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace core::math
{
    mat3 operator*(const mat3& a, const mat3& b) noexcept
    {
        glm::mat3 result = glm::make_mat3(a.m) * glm::make_mat3(b.m);
        mat3 out;
        const float* p = glm::value_ptr(result);
        for (int i = 0; i < 9; ++i)
        {
            out.m[i] = p[i];
        }
        return out;
    }

    vec3 operator*(const mat3& m, const vec3& v) noexcept
    {
        glm::vec3 result = glm::make_mat3(m.m) * glm::vec3{v.x, v.y, v.z};
        return vec3{result.x, result.y, result.z};
    }

    bool operator==(const mat3& a, const mat3& b) noexcept
    {
        return glm::make_mat3(a.m) == glm::make_mat3(b.m);
    }

    bool operator!=(const mat3& a, const mat3& b) noexcept
    {
        return glm::make_mat3(a.m) != glm::make_mat3(b.m);
    }

    mat3 inverse(const mat3& m) noexcept
    {
        glm::mat3 result = glm::inverse(glm::make_mat3(m.m));
        mat3 out;
        const float* p = glm::value_ptr(result);
        for (int i = 0; i < 9; ++i)
        {
            out.m[i] = p[i];
        }
        return out;
    }

    mat3 transpose(const mat3& m) noexcept
    {
        glm::mat3 result = glm::transpose(glm::make_mat3(m.m));
        mat3 out;
        const float* p = glm::value_ptr(result);
        for (int i = 0; i < 9; ++i)
        {
            out.m[i] = p[i];
        }
        return out;
    }
} // namespace core::math
