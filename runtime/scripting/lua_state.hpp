// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file lua_state.hpp
 * @brief The Lua side of the scripting subsystem, shared by the scripting
 *        implementation units.
 *
 * Private to the implementation files under runtime/scripting: this is the
 * one header that pulls in Lua and sol2, and the build hands their include
 * directories and sol2's configuration to those units only (root
 * CMakeLists.txt), so no other translation unit can — or needs to —
 * include it.
 */

#pragma once

// Lua's C API before sol2, so the Lua headers sol2 looks for resolve to
// these (their include guards are set) whatever else is on the system.
extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <sol/sol.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <core/os/directory_watcher.hpp>
#include <runtime/reflection.hpp>
#include <runtime/scripting/lua_behavior.hpp>
#include <runtime/scripting/script_host.hpp>

namespace runtime
{
    struct scene;
    struct node;

    namespace scripting
    {
        /**
         * @brief Aborts the binding being run with a Lua error carrying
         *        @p message.
         *
         * Throws; sol2 turns the exception into a Lua error at the binding
         * boundary, and the host's message handler adds the calling script's
         * file and line. Only for code running under a binding.
         */
        [[noreturn]] void raise(const std::string& message);

        /** @brief A node as a script holds it: the node's lifetime cell, never a raw pointer. */
        struct node_ref
        {
            std::shared_ptr<node* const> cell;

            /** @brief The node, or @c nullptr once it has been destroyed. */
            node* get() const noexcept
            {
                return cell ? *cell : nullptr;
            }

            /** @brief The node; @ref raise when it has been destroyed. */
            node& resolve() const;
        };

        /** @brief A handle to @p target for a script. */
        node_ref make_node_ref(node& target);

        /**
         * @brief A scene as a script holds it: the lifetime cell of the
         *        scene's root, which lives exactly as long as the scene.
         */
        struct scene_ref
        {
            std::shared_ptr<node* const> root;

            /** @brief The scene; @ref raise when it has been unloaded. */
            scene& resolve() const;
        };

        /** @brief One entry of a script's @c properties table. */
        struct script_property
        {
            std::string name;
            field_kind kind{field_kind::number};
            /** @brief The declared default. */
            sol::object initial;
            /** @brief A number declared as an integer, restored as one when it still is one. */
            bool integer{false};
        };

        /** @brief One loaded script, shared by every behaviour running it. */
        struct script_class
        {
            /** @brief The VFS path, normalised; the key the host finds it by. */
            std::string path;
            /** @brief The canonical key of the file it was read from, for the hot-reload watcher; empty for none. */
            std::string file_key;
            /** @brief What the script returned; invalid until it has loaded. */
            sol::table table;
            /** @brief The metatable of every behaviour's @c self: its @c __index is @ref table. */
            sol::table instance_meta;
            /** @brief The declared properties, in name order. */
            std::vector<script_property> properties;
            /** @brief Every behaviour running the script (loaded or waiting for it to load). */
            std::vector<lua_behavior*> behaviors;
            bool loaded{false};

            /** @brief The property named @p name, or @c nullptr. */
            const script_property* find_property(std::string_view name) const noexcept;
        };

        /** @brief @p value as a field value of @p property's kind, or @c std::nullopt when it is of another kind. */
        std::optional<field_value> to_field_value(const sol::object& value, const script_property& property);

        /** @brief @p value as a Lua value for @p property, or @c std::nullopt when it is of another kind. */
        std::optional<sol::object>
        to_lua_value(sol::state_view lua, const field_value& value, const script_property& property);

        /** @brief A copy of @p value that shares nothing mutable with it (a vector is copied, not aliased). */
        sol::object copy_value(sol::state_view lua, const sol::object& value);

        /** @brief Registers the math types: vec2, vec3, vec4, quat, mat4. */
        void bind_math(sol::state& lua);

        /** @brief Registers nodes, scenes, components, @c input and @c log (and @c print). */
        void bind_world(sol::state& lua);
    } // namespace scripting

    /** @brief One scripted behaviour's Lua side. */
    struct lua_behavior::instance
    {
        /** @brief The host's Lua side; null when there is no script (or the host is gone). */
        script_host::state* host{nullptr};
        /** @brief The script, owned by the host; null when there is none (or the host is gone). */
        scripting::script_class* script{nullptr};
        /** @brief The behaviour's @c self; invalid until the script has loaded. */
        sol::table self;
        /** @brief @c self.node is set (a hook has run on the attached behaviour). */
        bool node_bound{false};
        /** @brief The script's @c on_start has run. */
        bool started{false};
        /** @brief The script's @c on_enable ran more recently than its @c on_disable. */
        bool enabled{false};
        /** @brief An error stopped the behaviour. */
        bool failed{false};
    };

    struct script_host::state
    {
        state();

        sol::state lua;
        std::map<std::string, std::unique_ptr<scripting::script_class>, std::less<>> scripts;

        /** @brief A directory holding loaded scripts, polled for changes (Debug builds). */
        struct watch
        {
            std::string directory;
            core::os::directory_watcher watcher;
        };
        std::vector<watch> watches;
        double since_poll{0.0};

        /** @brief The script at @p path, loaded now if it is new (a failed load still returns it). */
        scripting::script_class& acquire(const std::string& path);

        /**
         * @brief Loads (or reloads) @p script from its file; false, with the
         *        error logged and @p script unchanged, when it does not load.
         */
        bool load(scripting::script_class& script);

        /** @brief Loads @p script again and re-binds every behaviour running it. */
        void reload(scripting::script_class& script);

        /** @brief A fresh @c self for @p script: its metatable set, the property defaults copied in. */
        sol::table make_self(const scripting::script_class& script);

        /**
         * @brief Calls @c self[hook](self[, *delta_time]) when @p self has such
         *        a function; false, with @p error set to the message and a
         *        traceback, when it raised an error.
         */
        bool call(const sol::table& self, const char* hook, const double* delta_time, std::string& error);

        /** @brief Starts watching the directory of @p script's file, if it is a real file. */
        void watch_file(scripting::script_class& script);

        /**
         * @brief Adds @p delta_seconds (real time) and, every half second,
         *        reloads the scripts whose files changed.
         */
        void poll_changes(double delta_seconds);

        /** @brief Re-binds @p behavior to its reloaded script; @p previous is the properties it was bound to. */
        void rebind(lua_behavior& behavior, const std::vector<scripting::script_property>& previous);

        /** @brief Drops @p behavior's hold on the Lua state (on shutdown); it stays idle from then on. */
        static void detach(lua_behavior& behavior);

        /** @brief @p behavior's @c self, or nil when it runs no loaded script. */
        static sol::object self_of(const lua_behavior& behavior);
    };
} // namespace runtime
