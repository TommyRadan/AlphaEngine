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
 * @file game_module.hpp
 * @brief The game-module entry point, @c GAME_MODULE().
 *
 * A game module is one translation unit under @c external/ that brings a
 * piece of the game into the world. It keeps no state of its own: its
 * @c GAME_MODULE() body is a bootstrap that runs once, when the engine has
 * brought every subsystem up, and spawns nodes, gives them components and
 * attaches the @ref runtime::behavior objects that carry the logic from then
 * on. Everything it makes belongs to a scene, so the scenes tear it down —
 * before the renderer — however the engine shuts down.
 *
 * @code
 * struct spinner final : runtime::behavior
 * {
 *     void on_update(float delta_time) override
 *     {
 *         m_angle += delta_time / 1000.0f;
 *         owner().transform.set_rotation(core::math::vec3{0.0f, 0.0f, m_angle});
 *     }
 *
 * private:
 *     float m_angle{0.0f};
 * };
 *
 * GAME_MODULE()
 * {
 *     runtime::node& prop = scene.create_node("prop");
 *     runtime::add_behavior<spinner>(prop);
 * }
 * @endcode
 *
 * The body receives @c scene, the engine's active scene at start-up (the
 * persistent scene); a module may load scenes of its own through
 * @c runtime::current_engine().scenes. A game-wide concern that belongs to
 * no object may still subscribe to the event bus from its bootstrap.
 */

#pragma once

#include <runtime/components/behavior_component.hpp>
#include <runtime/game_module.hpp>
#include <runtime/node.hpp>
#include <runtime/scene_graph.hpp>

/**
 * @brief Opens the definition of this translation unit's bootstrap — a
 *        function of @c runtime::context& @c scene — and registers it through
 *        @ref runtime::register_game_module. Use it once per module.
 */
#define GAME_MODULE()                                                                                                  \
    static void game_module_bootstrap(runtime::context& scene);                                                        \
    [[maybe_unused]] static const bool game_module_registered =                                                        \
        runtime::register_game_module(__FILE__, &game_module_bootstrap);                                               \
    static void game_module_bootstrap([[maybe_unused]] runtime::context& scene)
