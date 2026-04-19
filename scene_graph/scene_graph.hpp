/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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
 * @file scene_graph.hpp
 * @brief Scene graph subsystem entry point.
 */

#pragma once

#include <string>

#include <infrastructure/singleton.hpp>
#include <scene_graph/world.hpp>

namespace scene_graph
{
    /**
     * @brief Lifetime owner of the scene graph subsystem.
     *
     * Process-wide singleton that owns the ECS @ref world. @ref init is
     * called at startup and @ref quit at shutdown. Between those two calls,
     * the context subscribes to @c render_scene on the event bus and
     * iterates every entity with a @ref renderable_component, invoking its
     * @c draw callback. This replaces the older pattern of every game
     * module subscribing to @c render_scene independently.
     */
    struct context : public singleton<context>
    {
        /** @brief Initializes the scene graph subsystem and registers event listeners. */
        void init();

        /** @brief Shuts down the scene graph subsystem and clears the world. */
        void quit();

        /**
         * @brief Accessor for the ECS world owned by this subsystem.
         * @return Mutable reference to the process-wide @ref world.
         */
        world& get_world()
        {
            return m_world;
        }

        /** @brief Const overload of @ref get_world. */
        [[nodiscard]] const world& get_world() const
        {
            return m_world;
        }

    private:
        world m_world;
    };
} // namespace scene_graph
