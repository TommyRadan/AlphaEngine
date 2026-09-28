// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
 * The module goes by its file's name without @c _module
 * (@c fog_demo_module.cpp is @c fog_demo), and it runs only in a project
 * that names it (app/project.hpp): the project's @c modules list picks the
 * modules to install and the order their bootstraps run in.
 *
 * @code
 * struct spinner final : runtime::behavior
 * {
 *     void on_update(double delta_time) override
 *     {
 *         m_angle += static_cast<float>(delta_time);
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
 * The body receives @c scene, the persistent scene, which outlives every
 * scene loaded later; the project's startup scene, when it has one, is
 * already loaded and is the active scene. A module may load scenes of its
 * own through @c runtime::current_engine().scenes. A game-wide concern that
 * belongs to no object may still subscribe to the event bus from its
 * bootstrap; logic that runs every frame or every fixed step in a place of
 * its own in the frame is a system, added to one of the frame's stages
 * through @c runtime::current_engine().systems (runtime/scheduler.hpp) by a
 * behaviour that keeps the returned registration, so the system goes with
 * it.
 *
 * A module names its behaviours in a @c REFLECT_TYPES() block
 * (runtime/reflection.hpp) so scene files and prefabs can carry them. A
 * behaviour marks the fields worth saving from a @c static @c reflect
 * function, which reaches its private members:
 * @code
 * // In spinner:
 * static void reflect(runtime::type_builder<spinner>& type)
 * {
 *     type.field("angle", &spinner::m_angle);
 * }
 *
 * REFLECT_TYPES()
 * {
 *     registry.register_behavior<spinner>("spinner");
 * }
 * @endcode
 */

#pragma once

#include <runtime/components/behavior_component.hpp>
#include <runtime/game_module.hpp>
#include <runtime/node.hpp>
#include <runtime/reflection.hpp>
#include <runtime/scene.hpp>

/**
 * @brief Opens the definition of this translation unit's bootstrap — a
 *        function of @c runtime::scene& @c scene — and registers it through
 *        @ref runtime::register_game_module. Use it once per module.
 *
 * The registration is a static initialiser, so the module must be a source
 * of the executable (see external/CMakeLists.txt): the linker leaves out an
 * object of a static library that nothing references, and the initialiser
 * with it.
 */
#define GAME_MODULE()                                                                                                  \
    static void game_module_bootstrap(runtime::scene& scene);                                                          \
    [[maybe_unused]] static const bool game_module_registered =                                                        \
        runtime::register_game_module(__FILE__, &game_module_bootstrap);                                               \
    static void game_module_bootstrap([[maybe_unused]] runtime::scene& scene)
