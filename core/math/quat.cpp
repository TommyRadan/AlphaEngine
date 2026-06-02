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

#include <core/math/quat.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace core::math
{
    namespace
    {
        glm::quat to_glm(const quat& q) noexcept
        {
            return glm::quat{q.w, q.x, q.y, q.z};
        }

        quat from_glm(const glm::quat& q) noexcept
        {
            return quat{q.w, q.x, q.y, q.z};
        }
    } // namespace

    quat normalize(const quat& q) noexcept
    {
        return from_glm(glm::normalize(to_glm(q)));
    }

    quat inverse(const quat& q) noexcept
    {
        return from_glm(glm::inverse(to_glm(q)));
    }

    quat operator*(const quat& a, const quat& b) noexcept
    {
        return from_glm(to_glm(a) * to_glm(b));
    }

    vec3 operator*(const quat& q, const vec3& v) noexcept
    {
        glm::vec3 result = to_glm(q) * glm::vec3{v.x, v.y, v.z};
        return vec3{result.x, result.y, result.z};
    }

    quat quat_from_euler(const vec3& euler_radians) noexcept
    {
        return from_glm(glm::quat{glm::vec3{euler_radians.x, euler_radians.y, euler_radians.z}});
    }

    vec3 euler_from_quat(const quat& q) noexcept
    {
        glm::vec3 result = glm::eulerAngles(to_glm(q));
        return vec3{result.x, result.y, result.z};
    }

    quat quat_look_at(const vec3& direction, const vec3& up) noexcept
    {
        return from_glm(glm::quatLookAt(glm::normalize(glm::vec3{direction.x, direction.y, direction.z}),
                                        glm::vec3{up.x, up.y, up.z}));
    }

    mat4 to_mat4(const quat& q) noexcept
    {
        glm::mat4 result = glm::mat4_cast(to_glm(q));
        mat4 out;
        const float* p = glm::value_ptr(result);
        for (int i = 0; i < 16; ++i)
        {
            out.m[i] = p[i];
        }
        return out;
    }
} // namespace core::math
