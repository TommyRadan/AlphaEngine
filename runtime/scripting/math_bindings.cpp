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
 * @file math_bindings.cpp
 * @brief The @c core::math types as Lua values: vec2, vec3, vec4, quat and
 *        mat4 (runtime/scripting/lua_behavior.hpp lists what each offers).
 *
 * Each binds the engine wrapper, never GLM, and holds its value by copy:
 * a script's vector is its own. Components are bound through accessor
 * properties rather than member pointers: sol2's member-variable path does
 * not compile with Clang 18.
 */

#define LOG_CATEGORY "script"

#include <runtime/scripting/lua_state.hpp>

#include <cstdio>
#include <initializer_list>
#include <string>

#include <core/math/math.hpp>

namespace
{
    namespace math = core::math;

    // "name(a, b, c)", each number in its shortest form.
    std::string describe(const char* name, std::initializer_list<float> values)
    {
        std::string text = name;
        text += '(';
        bool first = true;
        for (const float value : values)
        {
            char number[32];
            std::snprintf(number, sizeof(number), "%g", static_cast<double>(value));
            text += first ? "" : ", ";
            text += number;
            first = false;
        }
        text += ')';
        return text;
    }

    // A read / write property over the float member @p Member of @p T.
    template<typename T, float T::*Member>
    auto component()
    {
        return sol::property([](const T& value) { return value.*Member; },
                             [](T& value, float component_value) { value.*Member = component_value; });
    }

    // What every vector type shares: the free functions as methods (also
    // callable as vecN.dot(a, b)), and the arithmetic every type supports.
    // Multiplication and division are left to the caller, which knows the
    // operand types beyond the scalar and the vector.
    template<typename V>
    void bind_vector(sol::usertype<V>& type)
    {
        type["dot"] = [](const V& a, const V& b) { return math::dot(a, b); };
        type["length"] = [](const V& value) { return math::length(value); };
        type["normalize"] = [](const V& value) { return math::normalize(value); };
        type["distance"] = [](const V& a, const V& b) { return math::distance(a, b); };
        type["lerp"] = [](const V& a, const V& b, float t) { return math::lerp(a, b, t); };
        type[sol::meta_function::addition] = [](const V& a, const V& b) { return a + b; };
        type[sol::meta_function::subtraction] = [](const V& a, const V& b) { return a - b; };
        type[sol::meta_function::unary_minus] = [](const V& value) { return -value; };
        type[sol::meta_function::equal_to] = [](const V& a, const V& b) { return a == b; };
        type[sol::meta_function::division] = sol::overload([](const V& a, const V& b) { return a / b; },
                                                           [](const V& value, float scalar) { return value / scalar; });
    }

    void bind_vec2(sol::state& lua)
    {
        using math::vec2;
        auto construct = sol::factories([] { return vec2{}; },
                                        [](float scalar) { return vec2{scalar}; },
                                        [](float x, float y) { return vec2{x, y}; });
        sol::usertype<vec2> type = lua.new_usertype<vec2>("vec2", sol::call_constructor, construct);
        type["new"] = construct;
        type["x"] = component<vec2, &vec2::x>();
        type["y"] = component<vec2, &vec2::y>();
        bind_vector(type);
        type[sol::meta_function::multiplication] =
            sol::overload([](const vec2& a, const vec2& b) { return a * b; },
                          [](const vec2& value, float scalar) { return value * scalar; },
                          [](float scalar, const vec2& value) { return scalar * value; });
        type[sol::meta_function::to_string] = [](const vec2& value) { return describe("vec2", {value.x, value.y}); };
    }

    void bind_vec3(sol::state& lua)
    {
        using math::vec3;
        auto construct = sol::factories([] { return vec3{}; },
                                        [](float scalar) { return vec3{scalar}; },
                                        [](float x, float y, float z) { return vec3{x, y, z}; });
        sol::usertype<vec3> type = lua.new_usertype<vec3>("vec3", sol::call_constructor, construct);
        type["new"] = construct;
        type["x"] = component<vec3, &vec3::x>();
        type["y"] = component<vec3, &vec3::y>();
        type["z"] = component<vec3, &vec3::z>();
        bind_vector(type);
        type["cross"] = [](const vec3& a, const vec3& b) { return math::cross(a, b); };
        type["world_up"] = [] { return math::world_up; };
        type["world_forward"] = [] { return math::world_forward; };
        type["world_right"] = [] { return math::world_right; };
        type[sol::meta_function::multiplication] =
            sol::overload([](const vec3& a, const vec3& b) { return a * b; },
                          [](const vec3& value, float scalar) { return value * scalar; },
                          [](float scalar, const vec3& value) { return scalar * value; });
        type[sol::meta_function::to_string] = [](const vec3& value)
        { return describe("vec3", {value.x, value.y, value.z}); };
    }

    void bind_vec4(sol::state& lua)
    {
        using math::vec3;
        using math::vec4;
        auto construct = sol::factories([] { return vec4{}; },
                                        [](float scalar) { return vec4{scalar}; },
                                        [](const vec3& xyz, float w) { return vec4{xyz, w}; },
                                        [](float x, float y, float z, float w) { return vec4{x, y, z, w}; });
        sol::usertype<vec4> type = lua.new_usertype<vec4>("vec4", sol::call_constructor, construct);
        type["new"] = construct;
        type["x"] = component<vec4, &vec4::x>();
        type["y"] = component<vec4, &vec4::y>();
        type["z"] = component<vec4, &vec4::z>();
        type["w"] = component<vec4, &vec4::w>();
        bind_vector(type);
        type[sol::meta_function::multiplication] =
            sol::overload([](const vec4& a, const vec4& b) { return a * b; },
                          [](const vec4& value, float scalar) { return value * scalar; },
                          [](float scalar, const vec4& value) { return scalar * value; },
                          [](const vec4& value, const math::mat4& matrix) { return value * matrix; });
        type[sol::meta_function::to_string] = [](const vec4& value)
        { return describe("vec4", {value.x, value.y, value.z, value.w}); };
    }

    void bind_quat(sol::state& lua)
    {
        using math::quat;
        using math::vec3;
        auto construct =
            sol::factories([] { return quat{}; }, [](float w, float x, float y, float z) { return quat{w, x, y, z}; });
        sol::usertype<quat> type = lua.new_usertype<quat>("quat", sol::call_constructor, construct);
        type["new"] = construct;
        type["w"] = component<quat, &quat::w>();
        type["x"] = component<quat, &quat::x>();
        type["y"] = component<quat, &quat::y>();
        type["z"] = component<quat, &quat::z>();
        type["from_euler"] = [](const vec3& euler_radians) { return math::quat_from_euler(euler_radians); };
        type["look_at"] =
            sol::overload([](const vec3& direction) { return math::quat_look_at(direction, math::world_up); },
                          [](const vec3& direction, const vec3& up) { return math::quat_look_at(direction, up); });
        type["from_basis"] = [](const vec3& x_axis, const vec3& y_axis, const vec3& z_axis)
        { return math::quat_from_basis(x_axis, y_axis, z_axis); };
        type["euler"] = [](const quat& rotation) { return math::euler_from_quat(rotation); };
        type["normalize"] = [](const quat& rotation) { return math::normalize(rotation); };
        type["inverse"] = [](const quat& rotation) { return math::inverse(rotation); };
        type["dot"] = [](const quat& a, const quat& b) { return math::dot(a, b); };
        type["slerp"] = [](const quat& a, const quat& b, float t) { return math::slerp(a, b, t); };
        type["nlerp"] = [](const quat& a, const quat& b, float t) { return math::nlerp(a, b, t); };
        type["to_mat4"] = [](const quat& rotation) { return math::to_mat4(rotation); };
        type[sol::meta_function::multiplication] =
            sol::overload([](const quat& a, const quat& b) { return a * b; },
                          [](const quat& rotation, const vec3& value) { return rotation * value; },
                          [](const quat& rotation, float scalar) { return rotation * scalar; },
                          [](float scalar, const quat& rotation) { return scalar * rotation; });
        type[sol::meta_function::addition] = [](const quat& a, const quat& b) { return a + b; };
        type[sol::meta_function::subtraction] = [](const quat& a, const quat& b) { return a - b; };
        type[sol::meta_function::unary_minus] = [](const quat& rotation) { return -rotation; };
        type[sol::meta_function::equal_to] = [](const quat& a, const quat& b)
        { return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z; };
        type[sol::meta_function::to_string] = [](const quat& rotation)
        { return describe("quat", {rotation.w, rotation.x, rotation.y, rotation.z}); };
    }

    // The storage index of a 1-based (column, row) pair, or an error.
    int element(int column, int row)
    {
        if (column < 1 || column > 4 || row < 1 || row > 4)
        {
            runtime::scripting::raise("mat4: column and row run from 1 to 4");
        }
        return (column - 1) * 4 + (row - 1);
    }

    void bind_mat4(sol::state& lua)
    {
        using math::mat4;
        using math::vec3;
        using math::vec4;
        auto construct = sol::factories([] { return mat4{}; }, [](float diagonal) { return mat4{diagonal}; });
        sol::usertype<mat4> type = lua.new_usertype<mat4>("mat4", sol::call_constructor, construct);
        type["new"] = construct;
        type["get"] = [](const mat4& matrix, int column, int row) { return matrix.m[element(column, row)]; };
        type["set"] = [](mat4& matrix, int column, int row, float value) { matrix.m[element(column, row)] = value; };
        type["translate"] =
            sol::overload([](const vec3& offset) { return math::translate(offset); },
                          [](const mat4& matrix, const vec3& offset) { return math::translate(matrix, offset); });
        type["rotate"] = sol::overload([](float angle, const vec3& axis) { return math::rotate(angle, axis); },
                                       [](const mat4& matrix, float angle, const vec3& axis)
                                       { return math::rotate(matrix, angle, axis); });
        type["scale"] =
            sol::overload([](const vec3& factors) { return math::scale(factors); },
                          [](const mat4& matrix, const vec3& factors) { return math::scale(matrix, factors); });
        type["look_at"] = [](const vec3& eye, const vec3& center, const vec3& up)
        { return math::look_at(eye, center, up); };
        type["perspective"] = [](float fov_y, float aspect, float near_z, float far_z)
        { return math::perspective(fov_y, aspect, near_z, far_z); };
        type["ortho"] = [](float left, float right, float bottom, float top, float near_z, float far_z)
        { return math::ortho(left, right, bottom, top, near_z, far_z); };
        type["inverse"] = [](const mat4& matrix) { return math::inverse(matrix); };
        type["transpose"] = [](const mat4& matrix) { return math::transpose(matrix); };
        type[sol::meta_function::multiplication] =
            sol::overload([](const mat4& a, const mat4& b) { return a * b; },
                          [](const mat4& matrix, const vec4& value) { return matrix * value; });
        type[sol::meta_function::equal_to] = [](const mat4& a, const mat4& b) { return a == b; };
        type[sol::meta_function::to_string] = [](const mat4& matrix)
        {
            const float* m = matrix.m;
            return describe(
                "mat4",
                {m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]});
        };
    }
} // namespace

void runtime::scripting::bind_math(sol::state& lua)
{
    bind_vec2(lua);
    bind_vec3(lua);
    bind_vec4(lua);
    bind_quat(lua);
    bind_mat4(lua);
}
