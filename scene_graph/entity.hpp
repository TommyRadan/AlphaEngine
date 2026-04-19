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
 * @file entity.hpp
 * @brief Opaque identifier type used by the scene_graph ECS world.
 */

#pragma once

#include <cstdint>

namespace scene_graph
{
    /**
     * @brief Opaque handle identifying an entity in the ECS @ref world.
     *
     * Identifiers are dense 32-bit unsigned integers allocated monotonically
     * by @ref world::create_entity. They are stable for the lifetime of the
     * entity but may be reused after destruction, so prefer not to retain
     * them across @ref world::destroy_entity calls without external
     * bookkeeping.
     */
    using entity_id = std::uint32_t;

    /**
     * @brief Sentinel @ref entity_id value reserved to mean "no entity".
     *
     * Never returned by @ref world::create_entity. Useful as a default
     * for uninitialized handles or to signal lookup failure.
     */
    inline constexpr entity_id invalid_entity = 0;
} // namespace scene_graph
