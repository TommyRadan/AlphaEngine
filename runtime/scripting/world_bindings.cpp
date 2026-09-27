// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file world_bindings.cpp
 * @brief Nodes, scenes, the built-in components, @c input and @c log as
 *        Lua values (runtime/scripting/lua_behavior.hpp lists the API).
 *
 * Nothing here hands Lua a pointer. A node is a @ref node_ref over its
 * lifetime cell, a scene a @ref scene_ref over its root's, and a component
 * a handle over its node that looks the component up on every use, so a
 * script can keep any of them past the object's life and only ever gets an
 * error for it. Structural changes a script asks for from inside a scene
 * walk go through the scene's deferred commands, as native code's must.
 */

#define LOG_CATEGORY "script"

#include <runtime/scripting/lua_state.hpp>

#include <string>
#include <utility>
#include <vector>

#include <core/input.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/lighting/spot_light.hpp>
#include <runtime/components/audio_source_component.hpp>
#include <runtime/components/behavior_component.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/rigidbody_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>

namespace
{
    namespace math = core::math;
    using runtime::scripting::make_node_ref;
    using runtime::scripting::node_ref;
    using runtime::scripting::raise;
    using runtime::scripting::scene_ref;

    std::string label(const runtime::node& target)
    {
        return target.name().empty() ? std::string{"<unnamed>"} : std::string{target.name().view()};
    }

    sol::optional<node_ref> optional_ref(runtime::node* target)
    {
        if (target == nullptr)
        {
            return sol::nullopt;
        }
        return make_node_ref(*target);
    }

    // True while @p scene is being walked, so a structural change must wait.
    bool walking(const runtime::scene* scene)
    {
        return scene != nullptr && scene->is_traversing();
    }

    void set_active(runtime::node& target, bool active)
    {
        runtime::scene* scene = target.scene();
        if (walking(scene))
        {
            scene->defer_set_active(target, active);
        }
        else
        {
            target.set_active(active);
        }
    }

    // Moves @p target under @p parent, or under its scene's root when
    // @p parent is null.
    void set_parent(runtime::node& target, runtime::node* parent)
    {
        runtime::scene* scene = target.scene();
        if (parent == nullptr)
        {
            if (scene == nullptr)
            {
                raise("node '" + label(target) + "' belongs to no scene, so it has no scene root to move under");
            }
            parent = &scene->root;
        }
        if (target.parent() == parent)
        {
            return;
        }
        runtime::scene* parent_scene = parent->scene();
        if (walking(scene) || walking(parent_scene))
        {
            (scene != nullptr ? scene : parent_scene)->defer_reparent(target, parent);
        }
        else
        {
            parent->add(target);
        }
    }

    // The @c C component of @p owner's node; an error when the node is gone
    // or no longer has one.
    template<typename C>
    C& component_of(const node_ref& owner, const char* what)
    {
        runtime::node& target = owner.resolve();
        C* found = target.get_component<C>();
        if (found == nullptr)
        {
            raise("node '" + label(target) + "' has no " + what + " any more");
        }
        return *found;
    }

    // --- Components ------------------------------------------------------------

    struct light_ref
    {
        node_ref owner;

        rendering_engine::light& resolve() const
        {
            rendering_engine::light* light = component_of<runtime::light_component>(owner, "light").get();
            if (light == nullptr)
            {
                raise("node '" + label(owner.resolve()) + "' has an empty light component");
            }
            return *light;
        }
    };

    const char* light_kind(const rendering_engine::light& light)
    {
        switch (light.type())
        {
        case rendering_engine::light_type::ambient:
            return "ambient";
        case rendering_engine::light_type::directional:
            return "directional";
        case rendering_engine::light_type::point:
            return "point";
        case rendering_engine::light_type::spot:
            return "spot";
        }
        return "unknown";
    }

    [[noreturn]] void no_such_light_field(const rendering_engine::light& light, const char* field)
    {
        raise(std::string{"a "} + light_kind(light) + " light has no " + field);
    }

    bool& cast_shadow_of(rendering_engine::light& light)
    {
        switch (light.type())
        {
        case rendering_engine::light_type::directional:
            return static_cast<rendering_engine::directional_light&>(light).cast_shadow;
        case rendering_engine::light_type::point:
            return static_cast<rendering_engine::point_light&>(light).cast_shadow;
        case rendering_engine::light_type::spot:
            return static_cast<rendering_engine::spot_light&>(light).cast_shadow;
        default:
            no_such_light_field(light, "cast_shadow");
        }
    }

    float& range_of(rendering_engine::light& light)
    {
        switch (light.type())
        {
        case rendering_engine::light_type::point:
            return static_cast<rendering_engine::point_light&>(light).range;
        case rendering_engine::light_type::spot:
            return static_cast<rendering_engine::spot_light&>(light).range;
        default:
            no_such_light_field(light, "range");
        }
    }

    rendering_engine::spot_light& spot_of(rendering_engine::light& light, const char* field)
    {
        if (light.type() != rendering_engine::light_type::spot)
        {
            no_such_light_field(light, field);
        }
        return static_cast<rendering_engine::spot_light&>(light);
    }

    struct camera_ref
    {
        node_ref owner;

        rendering_engine::camera& resolve() const
        {
            rendering_engine::camera* camera = component_of<runtime::camera_component>(owner, "camera").get();
            if (camera == nullptr)
            {
                raise("node '" + label(owner.resolve()) + "' has an empty camera component");
            }
            return *camera;
        }

        rendering_engine::perspective_camera& perspective(const char* field) const
        {
            auto* camera = dynamic_cast<rendering_engine::perspective_camera*>(&resolve());
            if (camera == nullptr)
            {
                raise(std::string{"only a perspective camera has a "} + field);
            }
            return *camera;
        }
    };

    struct rigidbody_ref
    {
        node_ref owner;

        runtime::rigidbody_component& resolve() const
        {
            return component_of<runtime::rigidbody_component>(owner, "rigidbody");
        }
    };

    struct audio_source_ref
    {
        node_ref owner;

        runtime::audio_source_component& resolve() const
        {
            return component_of<runtime::audio_source_component>(owner, "audio source");
        }
    };

    // A handle of type @p Ref to @p owner's @c C component, or nil when the
    // node has none.
    template<typename C, typename Ref>
    sol::optional<Ref> component_ref(const node_ref& owner)
    {
        if (!owner.resolve().has_component<C>())
        {
            return sol::nullopt;
        }
        return Ref{owner};
    }

    void bind_light(sol::table& types)
    {
        sol::usertype<light_ref> type = types.new_usertype<light_ref>("light", sol::no_constructor);
        type["kind"] =
            sol::readonly_property([](const light_ref& self) { return std::string{light_kind(self.resolve())}; });
        type["color"] =
            sol::property([](const light_ref& self) { return self.resolve().color; },
                          [](const light_ref& self, const math::vec3& color) { self.resolve().color = color; });
        type["intensity"] =
            sol::property([](const light_ref& self) { return self.resolve().intensity; },
                          [](const light_ref& self, float intensity) { self.resolve().intensity = intensity; });
        type["cast_shadow"] =
            sol::property([](const light_ref& self) { return cast_shadow_of(self.resolve()); },
                          [](const light_ref& self, bool cast) { cast_shadow_of(self.resolve()) = cast; });
        type["range"] = sol::property([](const light_ref& self) { return range_of(self.resolve()); },
                                      [](const light_ref& self, float range) { range_of(self.resolve()) = range; });
        type["inner_angle"] = sol::property(
            [](const light_ref& self) { return spot_of(self.resolve(), "inner_angle").inner_angle; },
            [](const light_ref& self, float angle) { spot_of(self.resolve(), "inner_angle").inner_angle = angle; });
        type["outer_angle"] = sol::property(
            [](const light_ref& self) { return spot_of(self.resolve(), "outer_angle").outer_angle; },
            [](const light_ref& self, float angle) { spot_of(self.resolve(), "outer_angle").outer_angle = angle; });
    }

    void bind_camera(sol::table& types)
    {
        sol::usertype<camera_ref> type = types.new_usertype<camera_ref>("camera", sol::no_constructor);
        type["priority"] =
            sol::property([](const camera_ref& self) { return self.resolve().get_priority(); },
                          [](const camera_ref& self, int priority) { self.resolve().set_priority(priority); });
        type["main"] = sol::property([](const camera_ref& self) { return self.resolve().is_main(); },
                                     [](const camera_ref& self, bool main) { self.resolve().set_main(main); });
        type["field_of_view"] = sol::property(
            [](const camera_ref& self) { return self.perspective("field_of_view").get_field_of_view(); },
            [](const camera_ref& self, float value) { self.perspective("field_of_view").set_field_of_view(value); });
        type["near_clip"] = sol::property(
            [](const camera_ref& self) { return self.perspective("near_clip").get_near_clip(); },
            [](const camera_ref& self, float value) { self.perspective("near_clip").set_near_clip(value); });
        type["far_clip"] = sol::property(
            [](const camera_ref& self) { return self.perspective("far_clip").get_far_clip(); },
            [](const camera_ref& self, float value) { self.perspective("far_clip").set_far_clip(value); });
    }

    void bind_rigidbody(sol::table& types)
    {
        sol::usertype<rigidbody_ref> type = types.new_usertype<rigidbody_ref>("rigidbody", sol::no_constructor);
        type["mass"] = sol::property([](const rigidbody_ref& self) { return self.resolve().mass(); },
                                     [](const rigidbody_ref& self, float mass) { self.resolve().set_mass(mass); });
        type["linear_velocity"] =
            sol::property([](const rigidbody_ref& self) { return self.resolve().linear_velocity(); },
                          [](const rigidbody_ref& self, const math::vec3& velocity)
                          { self.resolve().set_linear_velocity(velocity); });
        type["angular_velocity"] =
            sol::property([](const rigidbody_ref& self) { return self.resolve().angular_velocity(); },
                          [](const rigidbody_ref& self, const math::vec3& velocity)
                          { self.resolve().set_angular_velocity(velocity); });
        type["sleeping"] =
            sol::readonly_property([](const rigidbody_ref& self) { return self.resolve().is_sleeping(); });
        type["add_force"] = [](const rigidbody_ref& self, const math::vec3& force) { self.resolve().add_force(force); };
        type["add_force_at"] = [](const rigidbody_ref& self, const math::vec3& force, const math::vec3& point)
        { self.resolve().add_force_at(force, point); };
        type["add_torque"] = [](const rigidbody_ref& self, const math::vec3& torque)
        { self.resolve().add_torque(torque); };
        type["add_impulse"] = [](const rigidbody_ref& self, const math::vec3& impulse)
        { self.resolve().add_impulse(impulse); };
        type["add_impulse_at"] = [](const rigidbody_ref& self, const math::vec3& impulse, const math::vec3& point)
        { self.resolve().add_impulse_at(impulse, point); };
        type["add_angular_impulse"] = [](const rigidbody_ref& self, const math::vec3& impulse)
        { self.resolve().add_angular_impulse(impulse); };
        type["wake_up"] = [](const rigidbody_ref& self) { self.resolve().wake_up(); };
    }

    void bind_audio_source(sol::table& types)
    {
        sol::usertype<audio_source_ref> type =
            types.new_usertype<audio_source_ref>("audio_source", sol::no_constructor);
        type["play"] = [](const audio_source_ref& self) { self.resolve().play(); };
        type["stop"] = [](const audio_source_ref& self) { self.resolve().stop(); };
        type["playing"] =
            sol::readonly_property([](const audio_source_ref& self) { return self.resolve().is_playing(); });
        type["gain"] = sol::property([](const audio_source_ref& self) { return self.resolve().gain(); },
                                     [](const audio_source_ref& self, float gain) { self.resolve().set_gain(gain); });
        type["pitch"] =
            sol::property([](const audio_source_ref& self) { return self.resolve().pitch(); },
                          [](const audio_source_ref& self, float pitch) { self.resolve().set_pitch(pitch); });
    }

    // --- Nodes and scenes --------------------------------------------------------

    void bind_node(sol::table& types)
    {
        sol::usertype<node_ref> type = types.new_usertype<node_ref>("node", sol::no_constructor);
        type["valid"] = [](const node_ref& self) { return self.get() != nullptr; };
        type["name"] = sol::property([](const node_ref& self) { return std::string{self.resolve().name().view()}; },
                                     [](const node_ref& self, const std::string& name)
                                     { self.resolve().set_name(core::string_id{name}); });
        type["active"] = sol::property([](const node_ref& self) { return self.resolve().is_active(); },
                                       [](const node_ref& self, bool active) { set_active(self.resolve(), active); });
        type["active_in_hierarchy"] =
            sol::readonly_property([](const node_ref& self) { return self.resolve().is_effective_active(); });
        type["destroy_pending"] =
            sol::readonly_property([](const node_ref& self) { return self.resolve().is_destroy_pending(); });

        type["position"] = sol::property([](const node_ref& self) { return self.resolve().transform.get_position(); },
                                         [](const node_ref& self, const math::vec3& position)
                                         { self.resolve().transform.set_position(position); });
        type["rotation"] = sol::property([](const node_ref& self) { return self.resolve().transform.get_quaternion(); },
                                         [](const node_ref& self, const math::quat& rotation)
                                         { self.resolve().transform.set_quaternion(rotation); });
        type["euler"] = sol::property([](const node_ref& self) { return self.resolve().transform.get_rotation(); },
                                      [](const node_ref& self, const math::vec3& euler_radians)
                                      { self.resolve().transform.set_rotation(euler_radians); });
        type["scale"] = sol::property([](const node_ref& self) { return self.resolve().transform.get_scale(); },
                                      [](const node_ref& self, const math::vec3& scale)
                                      { self.resolve().transform.set_scale(scale); });
        type["world_position"] = sol::property([](const node_ref& self) { return self.resolve().world_position(); },
                                               [](const node_ref& self, const math::vec3& position)
                                               { self.resolve().set_world_position(position); });
        type["world_matrix"] =
            sol::readonly_property([](const node_ref& self) { return self.resolve().world_matrix(); });
        type["forward"] =
            sol::readonly_property([](const node_ref& self) { return self.resolve().transform.get_forward(); });
        type["right"] =
            sol::readonly_property([](const node_ref& self) { return self.resolve().transform.get_right(); });
        type["up"] = sol::readonly_property([](const node_ref& self) { return self.resolve().transform.get_up(); });
        type["look_at"] =
            sol::overload([](const node_ref& self, const math::vec3& target) { self.resolve().look_at(target); },
                          [](const node_ref& self, const math::vec3& target, const math::vec3& up)
                          { self.resolve().look_at(target, up); });

        type["parent"] = sol::property([](const node_ref& self) { return optional_ref(self.resolve().parent()); },
                                       [](const node_ref& self, const sol::object& parent)
                                       {
                                           runtime::node& target = self.resolve();
                                           if (parent.get_type() == sol::type::lua_nil)
                                           {
                                               set_parent(target, nullptr);
                                           }
                                           else if (parent.is<node_ref>())
                                           {
                                               set_parent(target, &parent.as<node_ref>().resolve());
                                           }
                                           else
                                           {
                                               raise("a node's parent is a node, or nil for the scene root");
                                           }
                                       });
        type["children"] = [](const node_ref& self)
        {
            std::vector<node_ref> children;
            for (runtime::node* child : self.resolve().children())
            {
                children.push_back(make_node_ref(*child));
            }
            return sol::as_table(std::move(children));
        };
        type["find"] = [](const node_ref& self, const std::string& name)
        { return optional_ref(self.resolve().find(core::string_id{name})); };
        type["scene"] = [](const node_ref& self) -> sol::optional<scene_ref>
        {
            runtime::scene* scene = self.resolve().scene();
            if (scene == nullptr)
            {
                return sol::nullopt;
            }
            return scene_ref{scene->root.lifetime_cell()};
        };

        type["light"] = [](const node_ref& self) { return component_ref<runtime::light_component, light_ref>(self); };
        type["camera"] = [](const node_ref& self)
        { return component_ref<runtime::camera_component, camera_ref>(self); };
        type["rigidbody"] = [](const node_ref& self)
        { return component_ref<runtime::rigidbody_component, rigidbody_ref>(self); };
        type["audio_source"] = [](const node_ref& self)
        { return component_ref<runtime::audio_source_component, audio_source_ref>(self); };
        // Another scripted behaviour's self, so scripts can talk to each other.
        type["script"] = [](const node_ref& self)
        {
            auto* holder = self.resolve().get_component<runtime::behavior_component>();
            const runtime::lua_behavior* logic = holder != nullptr ? holder->get_as<runtime::lua_behavior>() : nullptr;
            return logic != nullptr ? runtime::script_host::state::self_of(*logic) : sol::object{};
        };

        type[sol::meta_function::equal_to] = [](const node_ref& a, const node_ref& b) { return a.cell == b.cell; };
        type[sol::meta_function::to_string] = [](const node_ref& self)
        {
            const runtime::node* target = self.get();
            return target != nullptr ? "node '" + label(*target) + "'" : std::string{"node <destroyed>"};
        };
    }

    void bind_scene(sol::table& types)
    {
        sol::usertype<scene_ref> type = types.new_usertype<scene_ref>("scene", sol::no_constructor);
        type["valid"] = [](const scene_ref& self)
        {
            const runtime::node* root = self.root ? *self.root : nullptr;
            return root != nullptr && root->scene() != nullptr;
        };
        type["root"] = sol::readonly_property([](const scene_ref& self) { return make_node_ref(self.resolve().root); });
        // scene::create_node links the node at the end of the update when
        // the parent's scene is being walked; destroy_node always waits.
        type["create_node"] = [](const scene_ref& self, sol::optional<std::string> name, sol::optional<node_ref> parent)
        {
            runtime::scene& scene = self.resolve();
            runtime::node* under = parent ? &parent->resolve() : nullptr;
            return make_node_ref(scene.create_node(core::string_id{name ? *name : std::string{}}, under));
        };
        type["destroy_node"] = [](const scene_ref& self, const node_ref& target)
        {
            runtime::scene& scene = self.resolve();
            runtime::node& doomed = target.resolve();
            if (&doomed == &scene.root)
            {
                raise("the scene root cannot be destroyed");
            }
            scene.destroy_node(doomed);
        };
        type["find"] = [](const scene_ref& self, const std::string& name)
        { return optional_ref(self.resolve().find(core::string_id{name})); };
        type[sol::meta_function::equal_to] = [](const scene_ref& a, const scene_ref& b) { return a.root == b.root; };
        type[sol::meta_function::to_string] = [](const scene_ref& self)
        {
            const runtime::node* root = self.root ? *self.root : nullptr;
            return std::string{root != nullptr && root->scene() != nullptr ? "scene" : "scene <unloaded>"};
        };
    }

    // --- input and log -----------------------------------------------------------

    core::input& engine_input()
    {
        return *runtime::current_engine().input;
    }

    void bind_input(sol::state& lua)
    {
        sol::table input = lua.create_named_table("input");
        input["is_action_down"] = [](const std::string& name) { return engine_input().is_action_down(name); };
        input["was_action_pressed"] = [](const std::string& name) { return engine_input().was_action_pressed(name); };
        input["was_action_released"] = [](const std::string& name) { return engine_input().was_action_released(name); };
        input["get_axis"] = [](const std::string& name) { return engine_input().get_axis(name); };
        input["mouse_position"] = [] { return engine_input().mouse_position(); };
        input["mouse_delta"] = [] { return engine_input().mouse_delta(); };
        input["mouse_wheel_delta"] = [] { return engine_input().mouse_wheel_delta(); };
    }

    // log.<level>(...): the arguments joined as print joins them, logged
    // under the "script" category with the calling script's file and line.
    // Lua API only, so an error from a __tostring cannot skip a destructor.
    int log_at(lua_State* state, core::logging::verbosity level)
    {
        const int count = lua_gettop(state);
        luaL_Buffer buffer;
        luaL_buffinit(state, &buffer);
        for (int argument = 1; argument <= count; ++argument)
        {
            if (argument > 1)
            {
                luaL_addchar(&buffer, '\t');
            }
            luaL_tolstring(state, argument, nullptr);
            luaL_addvalue(&buffer);
        }
        luaL_pushresult(&buffer);

        lua_Debug caller{};
        const char* file = "?";
        unsigned line = 0;
        if (lua_getstack(state, 1, &caller) != 0 && lua_getinfo(state, "Sl", &caller) != 0)
        {
            file = caller.short_src;
            line = caller.currentline > 0 ? static_cast<unsigned>(caller.currentline) : 0;
        }
        core::logging::message(level, "script", file, line, "%s", lua_tostring(state, -1));
        return 0;
    }

    int log_info(lua_State* state)
    {
        return log_at(state, core::logging::verbosity::info);
    }

    int log_warn(lua_State* state)
    {
        return log_at(state, core::logging::verbosity::warn);
    }

    int log_error(lua_State* state)
    {
        return log_at(state, core::logging::verbosity::error);
    }

    void bind_log(sol::state& lua)
    {
        lua_State* raw = lua.lua_state();
        lua_newtable(raw);
        lua_pushcfunction(raw, &log_info);
        lua_setfield(raw, -2, "info");
        lua_pushcfunction(raw, &log_warn);
        lua_setfield(raw, -2, "warn");
        lua_pushcfunction(raw, &log_error);
        lua_setfield(raw, -2, "error");
        lua_setglobal(raw, "log");
        // print writes to the log too, rather than to a console nobody reads.
        lua_pushcfunction(raw, &log_info);
        lua_setglobal(raw, "print");
    }
} // namespace

// --- Handles -------------------------------------------------------------------

runtime::node& runtime::scripting::node_ref::resolve() const
{
    node* target = get();
    if (target == nullptr)
    {
        raise("the node has been destroyed");
    }
    return *target;
}

runtime::scripting::node_ref runtime::scripting::make_node_ref(node& target)
{
    return node_ref{target.lifetime_cell()};
}

runtime::scene& runtime::scripting::scene_ref::resolve() const
{
    node* root_node = root ? *root : nullptr;
    runtime::scene* scene = root_node != nullptr ? root_node->scene() : nullptr;
    if (scene == nullptr)
    {
        raise("the scene has been unloaded");
    }
    return *scene;
}

void runtime::scripting::bind_world(sol::state& lua)
{
    // The handle types are not globals (a script cannot make one); their
    // tables are kept in the registry, beside the metatables sol2 puts there.
    sol::table types = lua.create_table();
    bind_node(types);
    bind_scene(types);
    bind_light(types);
    bind_camera(types);
    bind_rigidbody(types);
    bind_audio_source(types);
    lua.registry()["alphaengine.script_types"] = types;

    bind_input(lua);
    bind_log(lua);
}
