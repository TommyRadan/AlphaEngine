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

#define LOG_CATEGORY "script"

#include <runtime/scripting/lua_behavior.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/scripting/lua_state.hpp>

namespace
{
    // The Lua side of the engine's script host, or null when it is not up.
    runtime::script_host::state* host_state()
    {
        runtime::script_host* host = runtime::current_engine().scripts.get();
        return host != nullptr ? host->get_state() : nullptr;
    }

    // One spelling per script: lexically normal, forward slashes.
    std::string normalise(const std::string& path)
    {
        if (path.empty())
        {
            return path;
        }
        const std::u8string text = core::platform::utf8_path(path).lexically_normal().generic_u8string();
        return std::string(text.begin(), text.end());
    }
} // namespace

runtime::lua_behavior::lua_behavior() : m_instance{std::make_unique<instance>()} {}

runtime::lua_behavior::lua_behavior(std::string script) : lua_behavior()
{
    set_script(std::move(script));
}

runtime::lua_behavior::~lua_behavior()
{
    release_script(false);
}

const std::string& runtime::lua_behavior::script() const noexcept
{
    return m_script;
}

bool runtime::lua_behavior::is_loaded() const noexcept
{
    const instance& state = *m_instance;
    return state.script != nullptr && state.script->loaded && state.self.valid();
}

bool runtime::lua_behavior::has_failed() const noexcept
{
    return m_instance->failed;
}

bool runtime::lua_behavior::set_script(std::string script)
{
    script = normalise(script);
    if (script == m_script && (script.empty() || is_loaded()))
    {
        return true;
    }

    release_script(true);
    m_script = std::move(script);
    if (m_script.empty())
    {
        return true;
    }

    script_host::state* host = host_state();
    if (host == nullptr)
    {
        LOG_ERR("lua_behavior: the script host is not running; '%s' is not loaded", m_script.c_str());
        return false;
    }
    instance& state = *m_instance;
    scripting::script_class& loaded = host->acquire(m_script);
    loaded.behaviors.push_back(this);
    state.host = host;
    state.script = &loaded;
    if (!loaded.loaded)
    {
        // The error is logged; a hot reload binds the behaviour once the
        // script loads.
        return false;
    }
    state.self = host->make_self(loaded);
    // Swapping the script of an enabled behaviour starts the new one as a
    // fresh behaviour would start.
    if (is_enabled())
    {
        on_enable();
    }
    return true;
}

void runtime::lua_behavior::release_script(bool end_hooks)
{
    instance& state = *m_instance;
    if (end_hooks && is_loaded() && !state.failed)
    {
        on_disable();
        if (state.node_bound)
        {
            call("on_destroy");
        }
    }
    if (state.script != nullptr)
    {
        std::erase(state.script->behaviors, this);
    }
    state.script = nullptr;
    state.host = nullptr;
    state.self = sol::table{};
    state.node_bound = false;
    state.started = false;
    state.enabled = false;
    state.failed = false;
}

void runtime::lua_behavior::call(const char* hook, const float* delta_time)
{
    instance& state = *m_instance;
    if (state.host == nullptr || !is_loaded() || state.failed)
    {
        return;
    }
    std::string error;
    bool succeeded = false;
    try
    {
        // self.node, once the behaviour runs on its node.
        if (!state.node_bound)
        {
            state.self.raw_set("node", scripting::make_node_ref(owner()));
            state.node_bound = true;
        }
        succeeded = state.host->call(state.self, hook, delta_time, error);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
    }
    if (!succeeded)
    {
        state.failed = true;
        const core::string_id& name = owner().name();
        LOG_ERR("lua_behavior on node '%s': %s of script '%s' failed; the behaviour is stopped: %s",
                name.empty() ? "<unnamed>" : name.c_str(),
                hook,
                m_script.c_str(),
                error.c_str());
    }
}

bool runtime::lua_behavior::ensure_started()
{
    instance& state = *m_instance;
    if (!is_loaded() || state.failed)
    {
        return false;
    }
    if (!state.started)
    {
        // Marked only once it has succeeded: an on_start that failed runs
        // again after a hot reload brings the behaviour back.
        call("on_start");
        state.started = !state.failed;
    }
    return !state.failed;
}

void runtime::lua_behavior::on_enable()
{
    instance& state = *m_instance;
    if (is_loaded() && !state.failed && !state.enabled)
    {
        state.enabled = true;
        call("on_enable");
    }
}

void runtime::lua_behavior::on_start()
{
    ensure_started();
}

void runtime::lua_behavior::on_fixed_update(float delta_time)
{
    if (ensure_started())
    {
        call("on_fixed_update", &delta_time);
    }
}

void runtime::lua_behavior::on_update(float delta_time)
{
    if (ensure_started())
    {
        call("on_update", &delta_time);
    }
}

void runtime::lua_behavior::on_disable()
{
    instance& state = *m_instance;
    if (is_loaded() && !state.failed && state.enabled)
    {
        state.enabled = false;
        call("on_disable");
    }
}

void runtime::lua_behavior::on_destroy()
{
    call("on_destroy");
}

std::unique_ptr<runtime::behavior> runtime::lua_behavior::clone() const
{
    auto copy = std::make_unique<lua_behavior>();
    copy->set_script(m_script);
    if (is_loaded() && copy->is_loaded())
    {
        const instance& source = *m_instance;
        instance& target = *copy->m_instance;
        for (const scripting::script_property& property : source.script->properties)
        {
            target.self.raw_set(
                property.name,
                scripting::copy_value(source.host->lua, source.self.raw_get<sol::object>(property.name)));
        }
    }
    return copy;
}

std::vector<runtime::field_info> runtime::lua_behavior::property_fields() const
{
    std::vector<field_info> fields;
    if (!is_loaded())
    {
        return fields;
    }
    for (const scripting::script_property& property : m_instance->script->properties)
    {
        field_info field;
        field.name = property.name;
        field.kind = property.kind;
        field.get = [name = property.name](const void* object)
        { return static_cast<const lua_behavior*>(object)->property(name).value_or(field_value{}); };
        field.set = [name = property.name](void* object, const field_value& value)
        { return static_cast<lua_behavior*>(object)->set_property(name, value); };
        fields.push_back(std::move(field));
    }
    return fields;
}

std::optional<runtime::field_value> runtime::lua_behavior::property(std::string_view name) const
{
    if (!is_loaded())
    {
        return std::nullopt;
    }
    const instance& state = *m_instance;
    const scripting::script_property* property = state.script->find_property(name);
    if (property == nullptr)
    {
        return std::nullopt;
    }
    if (std::optional<field_value> current =
            scripting::to_field_value(state.self.raw_get<sol::object>(property->name), *property))
    {
        return current;
    }
    // The script put something of another kind there; the default stands in.
    return scripting::to_field_value(property->initial, *property);
}

bool runtime::lua_behavior::set_property(std::string_view name, const field_value& value)
{
    if (!is_loaded())
    {
        return false;
    }
    instance& state = *m_instance;
    const scripting::script_property* property = state.script->find_property(name);
    if (property == nullptr)
    {
        return false;
    }
    std::optional<sol::object> converted = scripting::to_lua_value(state.host->lua, value, *property);
    if (!converted.has_value())
    {
        return false;
    }
    state.self.raw_set(property->name, *converted);
    return true;
}

// --- Host side ----------------------------------------------------------------

void runtime::script_host::state::rebind(lua_behavior& behavior,
                                         const std::vector<scripting::script_property>& previous)
{
    lua_behavior::instance& bound = *behavior.m_instance;
    const scripting::script_class& script = *bound.script;
    bound.failed = false;

    if (!bound.self.valid())
    {
        // The script had never loaded: the behaviour starts now, as a fresh
        // one would.
        bound.self = make_self(script);
        bound.node_bound = false;
        bound.started = false;
        bound.enabled = false;
        if (behavior.is_enabled())
        {
            behavior.on_enable();
        }
        return;
    }

    // Everything in self stays, except that the properties follow the new
    // declaration: one still declared with the same kind keeps its value, a
    // new one starts at its default, one no longer declared is cleared.
    for (const scripting::script_property& property : script.properties)
    {
        const auto before = std::find_if(previous.begin(),
                                         previous.end(),
                                         [&](const scripting::script_property& old)
                                         { return old.name == property.name && old.kind == property.kind; });
        const bool kept =
            before != previous.end() && bound.self.raw_get<sol::object>(property.name).get_type() != sol::type::lua_nil;
        if (!kept)
        {
            bound.self.raw_set(property.name, scripting::copy_value(lua, property.initial));
        }
    }
    for (const scripting::script_property& old : previous)
    {
        if (script.find_property(old.name) == nullptr)
        {
            bound.self.raw_set(old.name, sol::lua_nil);
        }
    }
}

void runtime::script_host::state::detach(lua_behavior& behavior)
{
    lua_behavior::instance& bound = *behavior.m_instance;
    bound.self = sol::table{};
    bound.script = nullptr;
    bound.host = nullptr;
    bound.node_bound = false;
}

sol::object runtime::script_host::state::self_of(const lua_behavior& behavior)
{
    if (!behavior.is_loaded())
    {
        return sol::object{};
    }
    return behavior.m_instance->self;
}

REFLECT_TYPES()
{
    registry.register_behavior<runtime::lua_behavior>("lua_behavior")
        .field("script", &runtime::lua_behavior::script, &runtime::lua_behavior::set_script)
        .asset("script")
        .construct_only()
        .instance_fields([](const runtime::lua_behavior& behavior) { return behavior.property_fields(); })
        .lossy_reason(
            [](const runtime::lua_behavior& behavior) -> std::string
            {
                if (behavior.script().empty() || behavior.is_loaded())
                {
                    return {};
                }
                return "script '" + behavior.script() + "' did not load, so its properties are not saved";
            });
}
