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

#include <runtime/scripting/lua_state.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <core/event.hpp>
#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>

namespace
{
    namespace scripting = runtime::scripting;

#if defined(_DEBUG)
    constexpr bool k_hot_reload = true;
#else
    constexpr bool k_hot_reload = false;
#endif

    // How often the hot-reload watchers rescan their directories.
    constexpr float k_poll_interval_ms = 500.0f;

    // The message handler of every protected call into a script: turns the
    // error object into a message that starts with the file and line of the
    // innermost script frame — errors a binding raises carry none of their
    // own — and appends a traceback. Built from the Lua API alone, so an
    // error inside it cannot skip a C++ destructor.
    int message_handler(lua_State* state)
    {
        if (lua_type(state, 1) != LUA_TSTRING)
        {
            if (luaL_callmeta(state, 1, "__tostring") != 0 && lua_type(state, -1) == LUA_TSTRING)
            {
                lua_replace(state, 1);
            }
            else
            {
                lua_pushfstring(state, "(error object is a %s value)", luaL_typename(state, 1));
                lua_replace(state, 1);
            }
        }
        lua_settop(state, 1);

        lua_Debug frame{};
        for (int level = 1; lua_getstack(state, level, &frame) != 0; ++level)
        {
            if (lua_getinfo(state, "Sl", &frame) == 0 || frame.currentline <= 0)
            {
                continue;
            }
            std::size_t length = 0;
            const char* message = lua_tolstring(state, 1, &length);
            const std::size_t source_length = std::strlen(frame.short_src);
            const bool located = length > source_length && std::strncmp(message, frame.short_src, source_length) == 0 &&
                                 message[source_length] == ':';
            if (!located)
            {
                lua_pushfstring(state, "%s:%d: %s", frame.short_src, frame.currentline, message);
                lua_replace(state, 1);
            }
            break;
        }

        luaL_traceback(state, state, lua_tostring(state, 1), 1);
        return 1;
    }

    // Called as invoke_hook(self, name, ...): runs self[name](self, ...) when
    // self has a function of that name, and nothing when it has none. Runs
    // inside the protected call, so a failing lookup is caught too.
    int invoke_hook(lua_State* state)
    {
        const int count = lua_gettop(state);
        lua_pushvalue(state, 2);
        lua_gettable(state, 1);
        if (lua_isnil(state, -1))
        {
            return 0;
        }
        if (lua_isfunction(state, -1) == 0)
        {
            return luaL_error(state, "'%s' is a %s, not a function", lua_tostring(state, 2), luaL_typename(state, -1));
        }
        lua_pushvalue(state, 1);
        for (int argument = 3; argument <= count; ++argument)
        {
            lua_pushvalue(state, argument);
        }
        lua_call(state, count - 1, 0);
        return 0;
    }

    // True when @p value is a number with an integer representation in Lua.
    bool is_integer(const sol::object& value)
    {
        lua_State* state = value.lua_state();
        value.push(state);
        const bool integer = lua_isinteger(state, -1) != 0;
        lua_pop(state, 1);
        return integer;
    }

    // The field kind a property default of @p value declares, or
    // std::nullopt for a value that cannot be a property.
    std::optional<runtime::field_kind> kind_of(const sol::object& value)
    {
        switch (value.get_type())
        {
        case sol::type::boolean:
            return runtime::field_kind::boolean;
        case sol::type::number:
            return runtime::field_kind::number;
        case sol::type::string:
            return runtime::field_kind::string;
        case sol::type::userdata:
            if (value.is<core::math::vec2>())
            {
                return runtime::field_kind::vec2;
            }
            if (value.is<core::math::vec3>())
            {
                return runtime::field_kind::vec3;
            }
            if (value.is<core::math::vec4>())
            {
                return runtime::field_kind::vec4;
            }
            if (value.is<core::math::quat>())
            {
                return runtime::field_kind::quat;
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    // Reads the properties table @p table declares, in name order.
    std::vector<scripting::script_property> read_properties(const std::string& path, const sol::table& table)
    {
        std::vector<scripting::script_property> properties;
        const sol::object declared = table.raw_get<sol::object>("properties");
        if (declared.get_type() == sol::type::lua_nil)
        {
            return properties;
        }
        if (declared.get_type() != sol::type::table)
        {
            LOG_WRN("script '%s': 'properties' is a %s, not a table; ignored",
                    path.c_str(),
                    sol::type_name(declared.lua_state(), declared.get_type()).c_str());
            return properties;
        }
        for (const auto& [key, value] : declared.as<sol::table>())
        {
            if (key.get_type() != sol::type::string)
            {
                LOG_WRN("script '%s': a property is not named by a string; ignored", path.c_str());
                continue;
            }
            std::string name = key.as<std::string>();
            if (name == "node")
            {
                LOG_WRN("script '%s': 'node' is the behaviour's node, not a property; ignored", path.c_str());
                continue;
            }
            const std::optional<runtime::field_kind> kind = kind_of(value);
            if (!kind.has_value())
            {
                LOG_WRN("script '%s': property '%s' defaults to a %s; a property is a boolean, number, string, "
                        "vec2, vec3, vec4 or quat, so it is ignored",
                        path.c_str(),
                        name.c_str(),
                        sol::type_name(value.lua_state(), value.get_type()).c_str());
                continue;
            }
            scripting::script_property property;
            property.name = std::move(name);
            property.kind = *kind;
            property.initial = value;
            property.integer = *kind == runtime::field_kind::number && is_integer(value);
            properties.push_back(std::move(property));
        }
        std::sort(properties.begin(),
                  properties.end(),
                  [](const scripting::script_property& a, const scripting::script_property& b)
                  { return a.name < b.name; });
        return properties;
    }
} // namespace

// --- Shared helpers ----------------------------------------------------------

void runtime::scripting::raise(const std::string& message)
{
    throw std::runtime_error{message};
}

const runtime::scripting::script_property*
runtime::scripting::script_class::find_property(std::string_view name) const noexcept
{
    for (const script_property& property : properties)
    {
        if (property.name == name)
        {
            return &property;
        }
    }
    return nullptr;
}

std::optional<runtime::field_value> runtime::scripting::to_field_value(const sol::object& value,
                                                                       const script_property& property)
{
    switch (property.kind)
    {
    case field_kind::boolean:
        if (value.get_type() == sol::type::boolean)
        {
            return field_value{value.as<bool>()};
        }
        return std::nullopt;
    case field_kind::number:
        if (value.get_type() == sol::type::number)
        {
            return field_value{static_cast<float>(value.as<double>())};
        }
        return std::nullopt;
    case field_kind::string:
        if (value.get_type() == sol::type::string)
        {
            return field_value{value.as<std::string>()};
        }
        return std::nullopt;
    case field_kind::vec2:
        if (value.is<core::math::vec2>())
        {
            return field_value{value.as<core::math::vec2>()};
        }
        return std::nullopt;
    case field_kind::vec3:
        if (value.is<core::math::vec3>())
        {
            return field_value{value.as<core::math::vec3>()};
        }
        return std::nullopt;
    case field_kind::vec4:
        if (value.is<core::math::vec4>())
        {
            return field_value{value.as<core::math::vec4>()};
        }
        return std::nullopt;
    case field_kind::quat:
        if (value.is<core::math::quat>())
        {
            return field_value{value.as<core::math::quat>()};
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<sol::object>
runtime::scripting::to_lua_value(sol::state_view lua, const field_value& value, const script_property& property)
{
    switch (property.kind)
    {
    case field_kind::boolean:
        if (const bool* flag = std::get_if<bool>(&value))
        {
            return sol::make_object(lua, *flag);
        }
        return std::nullopt;
    case field_kind::number:
    {
        double number = 0.0;
        if (const float* real = std::get_if<float>(&value))
        {
            number = static_cast<double>(*real);
        }
        else if (const int64_t* whole = std::get_if<int64_t>(&value))
        {
            number = static_cast<double>(*whole);
        }
        else
        {
            return std::nullopt;
        }
        // A property declared as an integer stays one while the value is whole.
        const auto whole = static_cast<lua_Integer>(number);
        if (property.integer && static_cast<double>(whole) == number)
        {
            return sol::make_object(lua, whole);
        }
        return sol::make_object(lua, number);
    }
    case field_kind::string:
        if (const std::string* text = std::get_if<std::string>(&value))
        {
            return sol::make_object(lua, *text);
        }
        return std::nullopt;
    case field_kind::vec2:
        if (const auto* vector = std::get_if<core::math::vec2>(&value))
        {
            return sol::make_object(lua, *vector);
        }
        return std::nullopt;
    case field_kind::vec3:
        if (const auto* vector = std::get_if<core::math::vec3>(&value))
        {
            return sol::make_object(lua, *vector);
        }
        return std::nullopt;
    case field_kind::vec4:
        if (const auto* vector = std::get_if<core::math::vec4>(&value))
        {
            return sol::make_object(lua, *vector);
        }
        return std::nullopt;
    case field_kind::quat:
        if (const auto* rotation = std::get_if<core::math::quat>(&value))
        {
            return sol::make_object(lua, *rotation);
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

sol::object runtime::scripting::copy_value(sol::state_view lua, const sol::object& value)
{
    // Booleans, numbers and strings are immutable in Lua; a math value is a
    // userdata a script can change in place, so it is copied.
    if (value.get_type() == sol::type::userdata)
    {
        if (value.is<core::math::vec2>())
        {
            return sol::make_object(lua, value.as<core::math::vec2>());
        }
        if (value.is<core::math::vec3>())
        {
            return sol::make_object(lua, value.as<core::math::vec3>());
        }
        if (value.is<core::math::vec4>())
        {
            return sol::make_object(lua, value.as<core::math::vec4>());
        }
        if (value.is<core::math::quat>())
        {
            return sol::make_object(lua, value.as<core::math::quat>());
        }
    }
    return value;
}

// --- script_host::state ------------------------------------------------------

runtime::script_host::state::state() = default;

runtime::scripting::script_class& runtime::script_host::state::acquire(const std::string& path)
{
    auto it = scripts.find(path);
    if (it != scripts.end())
    {
        // A script that failed to load is tried again: the file may have
        // been fixed (a Release build has no watcher to notice).
        if (!it->second->loaded)
        {
            load(*it->second);
        }
        return *it->second;
    }

    auto created = std::make_unique<scripting::script_class>();
    created->path = path;
    created->instance_meta = lua.create_table();
    scripting::script_class& script = *created;
    scripts.emplace(path, std::move(created));
    load(script);
    // Watched even when the load failed, so fixing the file brings it in.
    watch_file(script);
    return script;
}

bool runtime::script_host::state::load(scripting::script_class& script)
{
    std::string text;
    std::string error;
    if (!core::default_vfs().read_text_file(core::platform::utf8_path(script.path), text, &error))
    {
        LOG_ERR("script '%s' did not load: %s", script.path.c_str(), error.c_str());
        return false;
    }
    // Editors may save a UTF-8 byte-order mark, which the Lua parser rejects.
    constexpr std::string_view k_byte_order_mark = "\xEF\xBB\xBF";
    const std::size_t skip = std::string_view{text}.starts_with(k_byte_order_mark) ? k_byte_order_mark.size() : 0;

    lua_State* raw = lua.lua_state();
    const int top = lua_gettop(raw);
    lua_pushcfunction(raw, &message_handler);
    const std::string chunk_name = "@" + script.path;
    // Text only: a precompiled chunk could crash the interpreter.
    int status = luaL_loadbufferx(raw, text.data() + skip, text.size() - skip, chunk_name.c_str(), "t");
    if (status == LUA_OK)
    {
        // The chunk runs in an environment of its own that reads through to
        // the shared globals, so the globals one script assigns stay its own.
        lua_newtable(raw);
        lua_newtable(raw);
        lua_pushglobaltable(raw);
        lua_setfield(raw, -2, "__index");
        lua_setmetatable(raw, -2);
        lua_setupvalue(raw, -2, 1);
        status = lua_pcall(raw, 0, 1, top + 1);
    }
    if (status != LUA_OK)
    {
        const char* message = lua_tostring(raw, -1);
        LOG_ERR("script '%s' did not load: %s", script.path.c_str(), message != nullptr ? message : "(no message)");
        lua_settop(raw, top);
        return false;
    }
    if (lua_istable(raw, -1) == 0)
    {
        LOG_ERR("script '%s' did not load: it returned a %s, not a table", script.path.c_str(), luaL_typename(raw, -1));
        lua_settop(raw, top);
        return false;
    }
    sol::table table{raw, -1};
    lua_settop(raw, top);

    script.properties = read_properties(script.path, table);
    script.table = table;
    script.instance_meta.raw_set("__index", table);
    script.loaded = true;
    return true;
}

void runtime::script_host::state::reload(scripting::script_class& script)
{
    const std::vector<scripting::script_property> previous = script.properties;
    if (!load(script))
    {
        LOG_WRN("script '%s' changed but did not reload; the version already loaded keeps running",
                script.path.c_str());
        return;
    }
    // A copy: a hook run while re-binding may end a behaviour.
    const std::vector<lua_behavior*> bound = script.behaviors;
    for (lua_behavior* behavior : bound)
    {
        if (std::find(script.behaviors.begin(), script.behaviors.end(), behavior) != script.behaviors.end())
        {
            rebind(*behavior, previous);
        }
    }
    LOG_INF("Reloaded script '%s' (%zu behaviour(s))", script.path.c_str(), script.behaviors.size());
}

sol::table runtime::script_host::state::make_self(const scripting::script_class& script)
{
    sol::table self = lua.create_table();
    self[sol::metatable_key] = script.instance_meta;
    for (const scripting::script_property& property : script.properties)
    {
        self.raw_set(property.name, scripting::copy_value(lua, property.initial));
    }
    return self;
}

bool runtime::script_host::state::call(const sol::table& self,
                                       const char* hook,
                                       const float* delta_time,
                                       std::string& error)
{
    lua_State* raw = lua.lua_state();
    const int top = lua_gettop(raw);
    lua_pushcfunction(raw, &message_handler);
    lua_pushcfunction(raw, &invoke_hook);
    self.push(raw);
    lua_pushstring(raw, hook);
    int arguments = 2;
    if (delta_time != nullptr)
    {
        lua_pushnumber(raw, static_cast<lua_Number>(*delta_time));
        ++arguments;
    }
    const int status = lua_pcall(raw, arguments, 0, top + 1);
    if (status != LUA_OK)
    {
        const char* message = lua_tostring(raw, -1);
        error = message != nullptr ? message : "(no message)";
    }
    lua_settop(raw, top);
    return status == LUA_OK;
}

void runtime::script_host::state::watch_file(scripting::script_class& script)
{
    if constexpr (!k_hot_reload)
    {
        (void)script;
        return;
    }
    const core::vfs& files = core::default_vfs();
    const std::filesystem::path native = files.resolve(core::platform::utf8_path(script.path));
    const std::filesystem::path directory = native.parent_path();
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error))
    {
        return;
    }
    script.file_key = files.canonical_key(native);
    const std::string directory_key = files.canonical_key(directory);
    const bool watched = std::any_of(
        watches.begin(), watches.end(), [&](const watch& entry) { return entry.directory == directory_key; });
    if (!watched)
    {
        watches.push_back(watch{directory_key, core::platform::directory_watcher{directory, false}});
    }
}

void runtime::script_host::state::poll_changes(float delta_time)
{
    since_poll += delta_time;
    if (since_poll < k_poll_interval_ms)
    {
        return;
    }
    since_poll = 0.0f;

    const core::vfs& files = core::default_vfs();
    std::vector<scripting::script_class*> changed;
    for (watch& entry : watches)
    {
        for (const core::platform::file_change& change : entry.watcher.poll())
        {
            if (change.change == core::platform::file_change::kind::removed)
            {
                continue;
            }
            const std::string key = files.canonical_key(change.path);
            for (auto& [path, script] : scripts)
            {
                if (script->file_key == key && std::find(changed.begin(), changed.end(), script.get()) == changed.end())
                {
                    changed.push_back(script.get());
                }
            }
        }
    }
    for (scripting::script_class* script : changed)
    {
        reload(*script);
    }
}

// --- script_host -------------------------------------------------------------

runtime::script_host::script_host() = default;

runtime::script_host::~script_host()
{
    if (m_state)
    {
        quit();
    }
}

void runtime::script_host::init(core::event_bus& events)
{
    LOG_INF("Init Script Host (%s)", LUA_RELEASE);
    m_state = std::make_unique<state>();
    sol::state& lua = m_state->lua;

    // A small, safe library: nothing that reaches the file system or the
    // process (io, os, package), no debug library, and no way to run a file
    // behind the VFS's back or to load a precompiled chunk.
    lua.open_libraries(
        sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8, sol::lib::coroutine);
    lua["dofile"] = sol::lua_nil;
    lua["loadfile"] = sol::lua_nil;
    lua["load"] = sol::lua_nil;

    scripting::bind_math(lua);
    scripting::bind_world(lua);

    if constexpr (k_hot_reload)
    {
        m_reload_poll = events.subscribe<core::render_update>(
            [this](const core::render_update& tick)
            {
                if (m_state)
                {
                    m_state->poll_changes(tick.m_delta_time);
                }
            });
    }
    else
    {
        (void)events;
    }
}

void runtime::script_host::quit()
{
    LOG_INF("Quit Script Host");
    m_reload_poll.reset();
    if (!m_state)
    {
        return;
    }
    // Normally none is left: the scenes, and every behaviour in them, are
    // gone by now. One that is still alive lets go of the Lua state here and
    // stays idle.
    for (auto& [path, script] : m_state->scripts)
    {
        for (lua_behavior* behavior : script->behaviors)
        {
            state::detach(*behavior);
        }
        script->behaviors.clear();
    }
    m_state.reset();
}

runtime::script_host::state* runtime::script_host::get_state() noexcept
{
    return m_state.get();
}
