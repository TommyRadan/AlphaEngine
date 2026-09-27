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

#include <cmath>

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

    quat operator+(const quat& a, const quat& b) noexcept
    {
        return quat{a.w + b.w, a.x + b.x, a.y + b.y, a.z + b.z};
    }

    quat operator-(const quat& a, const quat& b) noexcept
    {
        return quat{a.w - b.w, a.x - b.x, a.y - b.y, a.z - b.z};
    }

    quat operator-(const quat& q) noexcept
    {
        return quat{-q.w, -q.x, -q.y, -q.z};
    }

    quat operator*(const quat& q, float s) noexcept
    {
        return quat{q.w * s, q.x * s, q.y * s, q.z * s};
    }

    quat operator*(float s, const quat& q) noexcept
    {
        return q * s;
    }

    float dot(const quat& a, const quat& b) noexcept
    {
        return glm::dot(to_glm(a), to_glm(b));
    }

    quat nlerp(const quat& a, const quat& b, float t) noexcept
    {
        // q and -q are the same rotation; pick the representative of b in
        // a's hemisphere so the blend takes the short way round.
        const quat target = dot(a, b) < 0.0f ? -b : b;
        const quat blended = a * (1.0f - t) + target * t;
        // Antipodal unit inputs cannot occur after the flip, so the blend
        // only degenerates for non-unit (zero) inputs; keep a zero as the
        // identity rather than dividing by it.
        if (dot(blended, blended) <= 0.0f)
        {
            return quat{};
        }
        return normalize(blended);
    }

    quat slerp(const quat& a, const quat& b, float t) noexcept
    {
        // Above this cosine (about 1.8 degrees apart) sin(theta) is small
        // enough that the slerp weights lose precision, while nlerp differs
        // from the true arc by far less than a float can resolve.
        constexpr float nearly_parallel = 0.9995f;

        float cosine = dot(a, b);
        quat target = b;
        if (cosine < 0.0f)
        {
            target = -b;
            cosine = -cosine;
        }
        if (cosine > nearly_parallel)
        {
            return nlerp(a, target, t);
        }

        const float theta = std::acos(cosine);
        const float sine = std::sin(theta);
        const float weight_a = std::sin((1.0f - t) * theta) / sine;
        const float weight_b = std::sin(t * theta) / sine;
        return normalize(a * weight_a + target * weight_b);
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

    quat quat_from_basis(const vec3& x_axis, const vec3& y_axis, const vec3& z_axis) noexcept
    {
        // glm::mat3's column constructor: each axis is where the matching
        // local axis lands, which is exactly the rotation matrix wanted.
        const glm::mat3 basis{glm::vec3{x_axis.x, x_axis.y, x_axis.z},
                              glm::vec3{y_axis.x, y_axis.y, y_axis.z},
                              glm::vec3{z_axis.x, z_axis.y, z_axis.z}};
        return from_glm(glm::normalize(glm::quat_cast(basis)));
    }

    quat quat_look_at(const vec3& direction, const vec3& up) noexcept
    {
        // Engine frame (see math.hpp): forward is +X, up is +Z, and the
        // right-hand side is forward x up (= -Y at identity). The local +Y
        // axis therefore maps onto the left, i.e. minus the right vector.
        const glm::vec3 forward = glm::normalize(glm::vec3{direction.x, direction.y, direction.z});
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{up.x, up.y, up.z}));
        const glm::vec3 true_up = glm::cross(right, forward);
        return quat_from_basis(vec3{forward.x, forward.y, forward.z},
                               vec3{-right.x, -right.y, -right.z},
                               vec3{true_up.x, true_up.y, true_up.z});
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
