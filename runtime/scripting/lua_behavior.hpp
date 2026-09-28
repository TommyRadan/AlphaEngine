// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file lua_behavior.hpp
 * @brief A behaviour whose logic is a Lua script.
 *
 * @ref runtime::lua_behavior is an ordinary native @ref runtime::behavior —
 * attached with @ref runtime::add_behavior, driven by the same
 * @ref runtime::behavior_component, saved by the same scene serializer —
 * that forwards each lifecycle hook to a script. The native path stays the
 * source of truth; the script is a thin attach point:
 * @code
 * runtime::add_behavior<runtime::lua_behavior>(prop, "scripts/bob.lua");
 * @endcode
 *
 * **The script** is a file read through the VFS. It returns a table — the
 * script's class — with any of the hooks @c on_start, @c on_enable,
 * @c on_disable, @c on_fixed_update(dt), @c on_update(dt) and
 * @c on_destroy, each called with @c self, when and as often as the native
 * hook of the same name (runtime/behavior.hpp; both deltas in milliseconds,
 * as there). It may declare a @c properties table of defaults:
 * @code
 * local bob = {
 *     properties = { height = 0.5, speed = 1.0 },
 * }
 *
 * function bob:on_start()
 *     self.base = self.node.position
 *     self.time = 0
 * end
 *
 * function bob:on_update(dt)
 *     self.time = self.time + dt / 1000
 *     self.node.position = self.base + vec3(0, 0, self.height * math.sin(self.time * self.speed))
 * end
 *
 * return bob
 * @endcode
 * Each behaviour gets its own @c self: a table whose fields are its state,
 * that falls back to the class for anything it does not hold (the hooks),
 * that starts with a copy of every property default, and whose @c node is
 * the node the behaviour is attached to. A property default may be a
 * boolean, a number, a string, or a @c vec2, @c vec3, @c vec4 or @c quat;
 * the properties are what a scene file saves of @c self (see
 * @ref lua_behavior::property_fields). A script runs in an environment of
 * its own, so a global it assigns stays private to it.
 *
 * **Errors.** An error a hook raises — a Lua error, or a misuse of the API
 * such as touching a destroyed node — is logged with the script file and
 * line and a traceback, and stops that behaviour: its script gets no further
 * calls (until a hot reload, see runtime/scripting/script_host.hpp). A
 * script that does not load (missing, a syntax error, not returning a
 * table) is logged the same way and leaves the behaviour idle. Nothing a
 * script does takes the engine down.
 *
 * **The API** a script sees, besides Lua's base, string, table, math, utf8
 * and coroutine libraries:
 * - @c vec2, @c vec3, @c vec4 — built as @c vec3(x, y, z) (or
 *   @c vec3.new), @c vec3(s), @c vec3() or @c vec4(xyz, w); fields @c x,
 *   @c y, @c z, @c w; @c + @c - @c * @c / (by a vector or a number),
 *   unary @c -, @c ==, @c tostring; @c dot, @c length, @c normalize,
 *   @c distance, @c lerp (and @c cross for @c vec3), callable as
 *   @c a:dot(b) or @c vec3.dot(a, b); @c vec3.world_up(),
 *   @c world_forward(), @c world_right(). Values, not references:
 *   @c node.position.x = 1 changes a copy, @c node.position = p moves the
 *   node.
 * - @c quat — @c quat() (identity), @c quat(w, x, y, z); fields @c w @c x
 *   @c y @c z; @c q * q, @c q * vec3 (rotates it), @c q * s, @c + @c -,
 *   unary @c -, @c ==; @c quat.from_euler(v), @c quat.look_at(dir[, up]),
 *   @c quat.from_basis(x, y, z), @c quat.slerp(a, b, t), @c quat.nlerp;
 *   @c q:euler(), @c q:normalize(), @c q:inverse(), @c q:dot(o),
 *   @c q:to_mat4().
 * - @c mat4 — @c mat4() (identity) or @c mat4(d); @c m:get(col, row) and
 *   @c m:set(col, row, v), 1-based; @c m * m, @c m * vec4, @c vec4 * m,
 *   @c ==; @c mat4.translate, @c rotate, @c scale (each also applied to a
 *   matrix given first), @c look_at, @c perspective, @c ortho;
 *   @c m:inverse(), @c m:transpose().
 * - a node (@c self.node, or any node a script reaches) — @c name,
 *   @c active, @c position, @c rotation (a @c quat), @c euler (radians),
 *   @c scale — local, readable and assignable; @c world_position
 *   (assignable), @c world_matrix, @c forward, @c right, @c up (in the
 *   parent's frame), @c active_in_hierarchy, @c destroy_pending (read
 *   only); @c look_at(target[, up]); @c parent (assignable, @c nil for the
 *   scene root), @c children(), @c find(name), @c scene(); @c light(),
 *   @c camera(), @c rigidbody(), @c audio_source() (@c nil when the node
 *   has none); @c script() (the @c self of the node's scripted behaviour,
 *   @c nil when it has none); @c valid(). Changing @c active or @c parent from a hook is
 *   applied at the end of the scene's update, as the native deferred
 *   commands are.
 * - a scene (@c node:scene()) — @c root, @c create_node([name[, parent]]),
 *   @c destroy_node(node) (at the end of the update, like
 *   @c scene::destroy_node), @c find(name), @c valid().
 * - components — a light: @c kind, @c color, @c intensity,
 *   @c cast_shadow, @c range, @c inner_angle, @c outer_angle (where the
 *   kind has them); a camera: @c priority, @c main, and for a perspective
 *   camera @c field_of_view, @c near_clip, @c far_clip; a rigidbody:
 *   @c mass, @c linear_velocity, @c angular_velocity, @c sleeping,
 *   @c add_force(f), @c add_force_at(f, p), @c add_torque(t),
 *   @c add_impulse(i), @c add_impulse_at(i, p), @c add_angular_impulse(i),
 *   @c wake_up(); an audio source: @c play(), @c stop(), @c playing,
 *   @c gain, @c pitch.
 * - @c input — @c is_action_down(name), @c was_action_pressed(name),
 *   @c was_action_released(name), @c get_axis(name), @c mouse_position(),
 *   @c mouse_delta(), @c mouse_wheel_delta(), over the actions and axes
 *   bound through @c core::input.
 * - @c log — @c info, @c warn and @c error, each taking any values as
 *   @c print does, logged under the @c "script" category with the calling
 *   script's file and line; @c print is @c log.info.
 *
 * **Lifetime.** A script never holds a pointer into the engine. A node
 * reaches it as a handle over the node's lifetime cell
 * (@ref node::lifetime_cell), a scene as one over its root's, and a
 * component as a handle over its node that looks the component up on every
 * use. A destroyed node reports @c valid() false, and any other use of it —
 * or of a component its node no longer has — raises an error in the script
 * rather than touching freed memory.
 *
 * **Serialization.** The type is registered as @c "lua_behavior": its
 * @c script field is the VFS path, set before the behaviour attaches, and
 * every declared property is a field of its own (see
 * @c type_info::instance_fields), so a scene file can attach a scripted
 * behaviour and tune it, and a save writes it back. An entry whose script
 * does not load is kept as a placeholder.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <runtime/behavior.hpp>
#include <runtime/reflection.hpp>
#include <runtime/scripting/script_host.hpp>

namespace runtime
{
    /**
     * @brief A behaviour that runs a Lua script; see the file notes.
     *
     * Needs a live engine with its script host up whenever it is given a
     * script. Main-thread-only, like every behaviour.
     */
    struct lua_behavior final : behavior
    {
        /** @brief The per-behaviour Lua side, defined by the scripting implementation. */
        struct instance;

        /** @brief A behaviour with no script yet (see @ref set_script); it does nothing. */
        lua_behavior();

        /** @brief A behaviour running the script at the VFS path @p script (see @ref set_script). */
        explicit lua_behavior(std::string script);

        ~lua_behavior() override;

        lua_behavior(const lua_behavior&) = delete;
        lua_behavior& operator=(const lua_behavior&) = delete;
        lua_behavior(lua_behavior&&) = delete;
        lua_behavior& operator=(lua_behavior&&) = delete;

        /** @brief The VFS path of the script, as given (normalised); empty for none. */
        const std::string& script() const noexcept;

        /**
         * @brief Runs the script at the VFS path @p script from now on (an
         *        empty path runs none); false, with the error logged, when it
         *        does not load.
         *
         * Meant for before the behaviour attaches: its @c self starts over
         * from the script's property defaults. On an attached behaviour the
         * old script is ended as if destroyed (@c on_disable, @c on_destroy)
         * and the new one started (@c on_enable, and @c on_start before its
         * next update). A script that does not load leaves the behaviour
         * idle until a hot reload brings it in.
         */
        bool set_script(std::string script);

        /** @brief True when the script has loaded and this behaviour is bound to it. */
        bool is_loaded() const noexcept;

        /** @brief True when an error in the script has stopped this behaviour. */
        bool has_failed() const noexcept;

        /**
         * @brief One field per property the script declares, in name order,
         *        reading and writing this behaviour's @c self; empty until
         *        the script has loaded.
         *
         * The field's kind follows the property's default: @c boolean,
         * @c number, @c string, @c vec2, @c vec3, @c vec4 or @c quat. Their
         * accessors take this behaviour, as @c type_info::instance_fields
         * requires.
         */
        std::vector<field_info> property_fields() const;

        /**
         * @brief The value of the property @p name in @c self, or its default
         *        when @c self holds something of another kind;
         *        @c std::nullopt when the script declares no such property.
         */
        std::optional<field_value> property(std::string_view name) const;

        /**
         * @brief Writes @p value into the property @p name of @c self; false
         *        when there is no such property or @p value is of another
         *        kind.
         */
        bool set_property(std::string_view name, const field_value& value);

        void on_enable() override;
        void on_start() override;
        void on_fixed_update(float delta_time) override;
        void on_update(float delta_time) override;
        void on_disable() override;
        void on_destroy() override;

        /**
         * @brief A behaviour running the same script, with this one's
         *        property values (not the rest of its @c self).
         */
        std::unique_ptr<behavior> clone() const override;

    private:
        // The host re-binds a behaviour on a hot reload and detaches it on
        // shutdown.
        friend struct script_host::state;

        // Calls the hook @p hook of the script (with @p delta_time when non-
        // null) if this behaviour is running one; an error stops it.
        void call(const char* hook, const float* delta_time = nullptr);

        // Runs the script's on_start if it has not run yet; false when the
        // behaviour cannot be updated (no script, stopped).
        bool ensure_started();

        // Ends the current script (on_disable / on_destroy when attached)
        // and forgets it.
        void release_script(bool end_hooks);

        std::string m_script;
        std::unique_ptr<instance> m_instance;
    };

    /**
     * @brief Registers @ref lua_behavior with @p registry as
     *        @c "lua_behavior", with its script and the script's properties
     *        as fields.
     *
     * Called once, by @ref default_type_registry when it is first used.
     */
    void register_lua_behavior(type_registry& registry);
} // namespace runtime
