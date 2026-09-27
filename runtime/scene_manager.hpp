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
 * @file scene_manager.hpp
 * @brief Loads, unloads, enables and updates the engine's scenes.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <core/string_id.hpp>

namespace runtime
{
    struct scene;

    /** @brief How @ref scene_manager::load treats the scenes already loaded. */
    enum class load_mode
    {
        /** Unload every other loaded scene (never the persistent one) and make the loaded one active. */
        single,
        /** Keep every loaded scene; the active scene does not change. */
        additive,
    };

    /**
     * @brief Owner of every scene: one persistent scene plus the scenes
     *        loaded on top of it.
     *
     * Owned by @ref runtime::engine as @c engine::scenes, with the usual
     * @ref init / @ref quit shape; @ref update runs from @c engine::tick.
     *
     * - The **persistent scene** exists from construction to destruction and
     *   is never unloaded, so a @ref load_mode::single load leaves it alone:
     *   it is the place for what lives across levels.
     * - @ref load makes a named scene. A freshly loaded scene is empty and the
     *   caller populates it (through @c scene::create_node, or from a scene
     *   file with @c runtime::load_scene, runtime/scene_serializer.hpp);
     *   loading a name that is already loaded returns that scene.
     * - @ref unload quits and destroys a scene: its nodes are freed and their
     *   components unwind their renderer, light and camera registrations.
     *   Requested while a scene is updating (from a component hook, say), the
     *   unload waits until every scene has finished its update.
     * - @ref active_scene is where new content goes by default: the scene the
     *   last single load made, the one @ref set_active_scene named, or the
     *   persistent scene when nothing else is loaded.
     *
     * **The renderer's registries follow the active set** — the loaded scenes
     * that are enabled. A scene's components register as they attach;
     * @ref set_enabled(scene, false) disables its root, so through
     * @c on_active_changed every mesh leaves the scene renderer, every light
     * leaves the light registry and every camera drops out of the arbitration,
     * and enabling it brings them back. A disabled scene still runs its update
     * (to apply queued commands) but updates no component. Unloading removes
     * its components for good.
     *
     * Scenes update in load order, the persistent scene first. Main-thread-only.
     */
    struct scene_manager
    {
        scene_manager();
        ~scene_manager();

        scene_manager(const scene_manager&) = delete;
        scene_manager& operator=(const scene_manager&) = delete;
        scene_manager(scene_manager&&) = delete;
        scene_manager& operator=(scene_manager&&) = delete;

        /** @brief Brings the persistent scene up. */
        void init();

        /**
         * @brief Unloads every loaded scene, newest first, then empties the
         *        persistent scene.
         *
         * Runs before the engine takes the renderer and asset cache down, so
         * every component still unwinds against live subsystems.
         */
        void quit();

        /** @brief Updates every loaded scene, then applies the unloads requested meanwhile. */
        void update();

        /**
         * @brief Loads (creates) the scene named @p name and returns it.
         *
         * With @ref load_mode::single every other scene except the persistent
         * one is unloaded and the loaded scene becomes the active scene; with
         * @ref load_mode::additive nothing else changes. A name that is already
         * loaded returns that scene (cancelling an unload still pending for
         * it), and @p name matching the persistent scene's name returns the
         * persistent scene.
         */
        scene& load(core::string_id name, load_mode mode = load_mode::single);

        /**
         * @brief Unloads the scene named @p name. Returns false (with a
         *        warning) when no such scene is loaded or it is the persistent
         *        scene.
         */
        bool unload(core::string_id name);

        /** @brief Unloads @p scene; see @ref unload(core::string_id). */
        bool unload(scene& scene);

        /** @brief The loaded scene named @p name, or @c nullptr. */
        scene* find(core::string_id name) noexcept;

        /** @brief The name @p scene was loaded under, or the empty id if it is not loaded here. */
        core::string_id name_of(const scene& scene) const noexcept;

        /** @brief The scene that is never unloaded. */
        scene& persistent_scene() noexcept;

        /** @brief The scene new content goes to by default. Never null. */
        scene& active_scene() noexcept;

        /** @brief Makes @p scene, which must be loaded here, the active scene. */
        void set_active_scene(scene& scene);

        /**
         * @brief Adds @p scene to, or takes it out of, the active set by
         *        enabling or disabling its root (see the class notes).
         *
         * Applied at the end of the scene's update when it is mid-traversal.
         */
        void set_enabled(scene& scene, bool enabled);

        /** @brief True while @p scene is in the active set (its root is enabled). */
        bool is_enabled(const scene& scene) const noexcept;

        /** @brief Number of loaded scenes, the persistent one included. */
        std::size_t scene_count() const noexcept;

        /**
         * @brief The loaded scene at @p index (0 is the persistent scene,
         *        the rest in load order).
         *
         * For enumerating every loaded scene, e.g. the debug overlay's
         * hierarchy panel; @p index must be less than @ref scene_count.
         */
        scene& scene_at(std::size_t index) noexcept;

        /** @brief The name the scene at @p index was loaded under ("persistent" for index 0). */
        core::string_id name_at(std::size_t index) const noexcept;

    private:
        struct entry
        {
            core::string_id name;
            std::unique_ptr<runtime::scene> scene;
            bool unload_pending{false};
        };

        entry* find_entry(const scene& scene) noexcept;
        const entry* find_entry(const scene& scene) const noexcept;

        // True while an unload now would pull a scene out from under a walk.
        bool busy() const noexcept;

        // Unloads every entry marked unload_pending, newest first.
        void apply_pending_unloads();

        // Quits and destroys the entry at @p index (never 0, the persistent scene).
        void destroy(std::size_t index);

        // [0] is the persistent scene; the rest in load order.
        std::vector<entry> m_scenes;
        scene* m_active{nullptr};
        bool m_updating{false};
    };
} // namespace runtime
