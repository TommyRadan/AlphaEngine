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

#include <core/math/mat4.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

namespace core::math
{
    namespace
    {
        mat4 store(const glm::mat4& src) noexcept
        {
            mat4 out;
            const float* p = glm::value_ptr(src);
            for (int i = 0; i < 16; ++i)
            {
                out.m[i] = p[i];
            }
            return out;
        }
    } // namespace

    mat4 operator*(const mat4& a, const mat4& b) noexcept
    {
        return store(glm::make_mat4(a.m) * glm::make_mat4(b.m));
    }

    vec4 operator*(const mat4& m, const vec4& v) noexcept
    {
        glm::vec4 result = glm::make_mat4(m.m) * glm::vec4{v.x, v.y, v.z, v.w};
        return vec4{result.x, result.y, result.z, result.w};
    }

    vec4 operator*(const vec4& v, const mat4& m) noexcept
    {
        glm::vec4 result = glm::vec4{v.x, v.y, v.z, v.w} * glm::make_mat4(m.m);
        return vec4{result.x, result.y, result.z, result.w};
    }

    bool operator==(const mat4& a, const mat4& b) noexcept
    {
        return glm::make_mat4(a.m) == glm::make_mat4(b.m);
    }

    bool operator!=(const mat4& a, const mat4& b) noexcept
    {
        return glm::make_mat4(a.m) != glm::make_mat4(b.m);
    }

    mat4& operator*=(mat4& a, const mat4& b) noexcept
    {
        a = a * b;
        return a;
    }

    mat4 look_at(const vec3& eye, const vec3& center, const vec3& up) noexcept
    {
        return store(glm::lookAt(
            glm::vec3{eye.x, eye.y, eye.z}, glm::vec3{center.x, center.y, center.z}, glm::vec3{up.x, up.y, up.z}));
    }

    mat4 perspective(float fov_y, float aspect, float near_z, float far_z) noexcept
    {
        return store(glm::perspective(fov_y, aspect, near_z, far_z));
    }

    mat4 ortho(float left, float right, float bottom, float top, float near_z, float far_z) noexcept
    {
        return store(glm::ortho(left, right, bottom, top, near_z, far_z));
    }

    mat4 translate(const vec3& v) noexcept
    {
        return store(glm::translate(glm::vec3{v.x, v.y, v.z}));
    }

    mat4 translate(const mat4& m, const vec3& v) noexcept
    {
        return store(glm::translate(glm::make_mat4(m.m), glm::vec3{v.x, v.y, v.z}));
    }

    mat4 rotate(float angle, const vec3& axis) noexcept
    {
        return store(glm::rotate(angle, glm::vec3{axis.x, axis.y, axis.z}));
    }

    mat4 rotate(const mat4& m, float angle, const vec3& axis) noexcept
    {
        return store(glm::rotate(glm::make_mat4(m.m), angle, glm::vec3{axis.x, axis.y, axis.z}));
    }

    mat4 scale(const vec3& v) noexcept
    {
        return store(glm::scale(glm::vec3{v.x, v.y, v.z}));
    }

    mat4 scale(const mat4& m, const vec3& v) noexcept
    {
        return store(glm::scale(glm::make_mat4(m.m), glm::vec3{v.x, v.y, v.z}));
    }

    mat4 inverse(const mat4& m) noexcept
    {
        return store(glm::inverse(glm::make_mat4(m.m)));
    }

    mat4 transpose(const mat4& m) noexcept
    {
        return store(glm::transpose(glm::make_mat4(m.m)));
    }
} // namespace core::math
