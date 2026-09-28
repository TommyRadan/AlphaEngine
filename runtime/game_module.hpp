// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file game_module.hpp
 * @brief Registry of the game modules' bootstraps, by name, and the hook
 *        that runs one.
 *
 * A game module (a translation unit under @c external/) declares one
 * bootstrap with the @c GAME_MODULE() macro from
 * @c external/api/game_module.hpp. The macro registers it here at static
 * initialisation time — before @c main and so before any engine exists —
 * under the module's name: its source file's name without the directory,
 * the extension and a trailing @c _module (@c external/fog_demo_module.cpp
 * registers @c fog_demo). An executable links every module it offers; the
 * project it runs names the ones to install (app/project.hpp), and the
 * application runs their bootstraps through @ref install_game_module once
 * the engine is up. That is the whole of the engine's contact with the
 * game: a bootstrap spawns nodes and attaches their components and
 * behaviours, and from then on the game runs as those objects' hooks, owned
 * and torn down by the scenes.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace runtime
{
    struct scene;

    /**
     * @brief A game module's bootstrap: spawns the module's nodes into
     *        @p scene (or into scenes it loads) and attaches their behaviours.
     */
    using game_module_bootstrap = void (*)(scene& scene);

    /**
     * @brief Registers @p bootstrap under the name of the source file
     *        @p source (see the file notes). Returns true, so a static
     *        initialiser can call it.
     *
     * Called by @c GAME_MODULE(); safe during static initialisation. A
     * second module under a name already taken is never installed: the
     * first registration keeps the name.
     */
    bool register_game_module(const char* source, game_module_bootstrap bootstrap);

    /** @brief The names of every registered module, in registration order. */
    std::vector<std::string> game_module_names();

    /** @brief Whether a module is registered under @p name. */
    bool has_game_module(std::string_view name);

    /**
     * @brief The install hook: runs the bootstrap of the module named
     *        @p name against @p scene.
     *
     * Called by the application once every subsystem is up, once per module
     * its project names, in the project's order, with the persistent scene.
     * An exception thrown by the bootstrap propagates to the caller.
     *
     * @return false, having run nothing, when no module is registered under
     *         @p name.
     */
    bool install_game_module(std::string_view name, scene& scene);
} // namespace runtime
