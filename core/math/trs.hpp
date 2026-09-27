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
 * @file trs.hpp
 * @brief A decomposed affine pose: translation, rotation and scale.
 */

#pragma once

#include <core/math/mat4.hpp>
#include <core/math/quat.hpp>
#include <core/math/vec3.hpp>

namespace core::math
{
    /**
     * @brief A local pose held as its three parts rather than as a matrix,
     *        so two poses can be blended part by part.
     *
     * The matrix it stands for is @c translate(translation) *
     * to_mat4(rotation) * scale(scale) — scale first, then rotation, then
     * translation — the order @c util::transform composes and glTF node
     * poses are authored in. A skeleton joint's bind pose and every sampled
     * animation pose are @c trs values.
     */
    struct trs
    {
        vec3 translation{0.0f, 0.0f, 0.0f};
        quat rotation{};
        vec3 scale{1.0f, 1.0f, 1.0f};
    };

    /**
     * @brief Blends two poses: translation and scale linearly, rotation by
     *        @ref slerp along the shortest arc. @p t = 0 gives @p a, 1 gives
     *        @p b.
     */
    trs lerp(const trs& a, const trs& b, float t) noexcept;

    /** @brief The matrix @c T * R * S the pose stands for. */
    mat4 to_mat4(const trs& pose) noexcept;
} // namespace core::math
