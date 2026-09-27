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
 * @file game_module.hpp
 * @brief Registry of the game modules' bootstraps and the hook that runs them.
 *
 * A game module (a translation unit under @c external/) declares one
 * bootstrap with the @c GAME_MODULE() macro from
 * @c external/api/game_module.hpp. The macro registers it here at static
 * initialisation time — before @c main and so before any engine exists — and
 * @ref runtime::engine::init runs every registered bootstrap through
 * @ref install_game_modules once all subsystems are up. That is the whole of
 * the engine's contact with the game: a bootstrap spawns nodes and attaches
 * their components and behaviours, and from then on the game runs as those
 * objects' hooks, owned and torn down by the scenes.
 */

#pragma once

namespace runtime
{
    struct context;

    /**
     * @brief A game module's bootstrap: spawns the module's nodes into
     *        @p scene (or into scenes it loads) and attaches their behaviours.
     */
    using game_module_bootstrap = void (*)(context& scene);

    /**
     * @brief Registers @p bootstrap, from the source file @p source (named in
     *        the log), to run when the engine installs the game. Returns
     *        true, so a static initialiser can call it.
     *
     * Called by @c GAME_MODULE(); safe during static initialisation.
     */
    bool register_game_module(const char* source, game_module_bootstrap bootstrap);

    /**
     * @brief The install hook: runs every registered bootstrap against
     *        @p scene, in registration order.
     *
     * Called by @ref runtime::engine::init, once every subsystem is up, with
     * the active scene (the persistent scene at that point). The order of
     * registrations across translation units is unspecified, so a bootstrap
     * must not rely on another module's nodes existing yet; a behaviour's
     * @c on_start, which runs on the first update, can. An exception thrown
     * by a bootstrap propagates to the caller.
     */
    void install_game_modules(context& scene);
} // namespace runtime
