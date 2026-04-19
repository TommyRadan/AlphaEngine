/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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
 * @file world.hpp
 * @brief Placeholder ECS world type embedded in every @ref scene_graph::scene.
 *
 * The real ECS @c world is being implemented in parallel on the
 * @c feat/ecs-world branch (issue #16). Until that lands this empty
 * struct keeps the @ref scene_graph::scene API stable; once the ECS
 * world is merged, this header will be superseded and the @c game_world
 * member of @ref scene_graph::scene will become the real type.
 */

#pragma once

namespace scene_graph
{
    /**
     * @brief Placeholder ECS world used by @ref scene until issue #16 lands.
     *
     * Intentionally empty — existence of the type is enough to pin the
     * @ref scene ABI while the ECS implementation is in flight on a
     * separate branch.
     */
    struct world
    {
    };
} // namespace scene_graph
