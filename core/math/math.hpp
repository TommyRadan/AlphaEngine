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
 * @file math.hpp
 * @brief Umbrella include for the engine-owned math types.
 *
 * Each vector, matrix, and quaternion type lives in its own header next to
 * this one. The implementations are in matching @c .cpp files, which are
 * the only translation units that include and name @c glm:: . Including
 * this header pulls every public math type into scope while keeping GLM
 * out of the consumer's preprocessor input.
 */

#pragma once

#include <core/math/aabb.hpp>
#include <core/math/frustum.hpp>
#include <core/math/mat3.hpp>
#include <core/math/mat4.hpp>
#include <core/math/quat.hpp>
#include <core/math/sphere.hpp>
#include <core/math/vec2.hpp>
#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>

namespace core::math
{
    float lerp(float a, float b, float t) noexcept;
} // namespace core::math
