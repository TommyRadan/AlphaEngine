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

#include <runtime/scene_serializer.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <locale>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>
#include <runtime/behavior.hpp>
#include <runtime/components/behavior_component.hpp>
#include <runtime/components/placeholder_component.hpp>
#include <runtime/node.hpp>
#include <runtime/reflection.hpp>
#include <runtime/scene_graph.hpp>
#include <runtime/scene_manager.hpp>

namespace runtime
{
    using json = nlohmann::ordered_json;

    struct scene_document::contents
    {
        json document;
    };

    // The serializer's access to a document's parsed contents.
    struct scene_io
    {
        static const json& contents_of(const scene_document& document)
        {
            return document.m_data->document;
        }

        static scene_document make(json document)
        {
            auto data = std::make_shared<scene_document::contents>();
            data->document = std::move(document);
            return scene_document{std::move(data)};
        }
    };

    namespace
    {
        constexpr const char* k_format = "alphaengine.scene";
        constexpr int64_t k_version = 1;

        // A quaternion further than this from unit length was not written by
        // a save (which stores the transform's normalised rotation); it is
        // normalised on load rather than restored bit for bit.
        constexpr float k_unit_tolerance = 1e-4f;

        // -- Load sessions ------------------------------------------------------

        // What keep_alive_while_loading holds for the instantiate in
        // progress; nested instantiations each get their own.
        struct load_session
        {
            load_session() : m_previous{current()}
            {
                current() = this;
            }

            ~load_session()
            {
                current() = m_previous;
            }

            load_session(const load_session&) = delete;
            load_session& operator=(const load_session&) = delete;

            static load_session*& current()
            {
                static load_session* active = nullptr;
                return active;
            }

            std::vector<std::shared_ptr<const void>> held;

        private:
            load_session* m_previous;
        };

        // -- Encoding -----------------------------------------------------------

        // The JSON number a float field is written as: the shortest decimal
        // that reads back as the same float, so a file shows 0.25 rather than
        // 0.25000000372529. Falls back to the float's exact double value in the
        // (theoretical) case where reading that decimal as a double and
        // narrowing it would not give the float back.
        double to_json_number(float value)
        {
            if (!std::isfinite(value))
            {
                return static_cast<double>(value);
            }
            char buffer[32];
            const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (result.ec != std::errc{})
            {
                return static_cast<double>(value);
            }
            std::istringstream stream{std::string{buffer, result.ptr}};
            stream.imbue(std::locale::classic());
            double parsed = 0.0;
            if (stream >> parsed && static_cast<float>(parsed) == value)
            {
                return parsed;
            }
            return static_cast<double>(value);
        }

        json number_array(std::initializer_list<float> values)
        {
            json out = json::array();
            for (const float value : values)
            {
                out.push_back(to_json_number(value));
            }
            return out;
        }

        json encode_value(const type_registry& registry, const field_value& value, const field_info* field);

        json encode_object(const type_registry& registry, const object_value& value)
        {
            if (value.empty())
            {
                return json(nullptr);
            }
            const type_info* type = registry.find(value.type);
            json fields = json::object();
            for (const field_entry& entry : value.fields)
            {
                const field_info* field = type != nullptr ? type->find_field(entry.name) : nullptr;
                fields[entry.name] = encode_value(registry, entry.value, field);
            }
            json out = json::object();
            out["type"] = value.type;
            out["fields"] = std::move(fields);
            return out;
        }

        // @p field supplies the enumerator names; null encodes by value alone.
        json encode_value(const type_registry& registry, const field_value& value, const field_info* field)
        {
            return std::visit(
                [&](const auto& alternative) -> json
                {
                    using alternative_type = std::decay_t<decltype(alternative)>;
                    if constexpr (std::is_same_v<alternative_type, bool>)
                    {
                        return json(alternative);
                    }
                    else if constexpr (std::is_same_v<alternative_type, int64_t>)
                    {
                        if (field != nullptr && field->kind == field_kind::enumeration)
                        {
                            if (const enum_entry* named = field->find_enumerator(alternative))
                            {
                                return json(named->name);
                            }
                        }
                        return json(alternative);
                    }
                    else if constexpr (std::is_same_v<alternative_type, float>)
                    {
                        return json(to_json_number(alternative));
                    }
                    else if constexpr (std::is_same_v<alternative_type, core::math::vec2>)
                    {
                        return number_array({alternative.x, alternative.y});
                    }
                    else if constexpr (std::is_same_v<alternative_type, core::math::vec3>)
                    {
                        return number_array({alternative.x, alternative.y, alternative.z});
                    }
                    else if constexpr (std::is_same_v<alternative_type, core::math::vec4>)
                    {
                        return number_array({alternative.x, alternative.y, alternative.z, alternative.w});
                    }
                    else if constexpr (std::is_same_v<alternative_type, core::math::quat>)
                    {
                        return number_array({alternative.w, alternative.x, alternative.y, alternative.z});
                    }
                    else if constexpr (std::is_same_v<alternative_type, std::string>)
                    {
                        return json(alternative);
                    }
                    else
                    {
                        return encode_object(registry, alternative);
                    }
                },
                value);
        }

        // -- Decoding -----------------------------------------------------------

        // Exactly @p count numbers, or std::nullopt.
        std::optional<std::array<float, 4>> read_numbers(const json& value, std::size_t count)
        {
            if (!value.is_array() || value.size() != count)
            {
                return std::nullopt;
            }
            std::array<float, 4> out{};
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!value[i].is_number())
                {
                    return std::nullopt;
                }
                out[i] = static_cast<float>(value[i].get<double>());
            }
            return out;
        }

        std::optional<field_value>
        decode_value(const type_registry& registry, const field_info& field, const json& value, std::string& problem);

        std::optional<object_value>
        decode_object(const type_registry& registry, const json& value, std::string& problem)
        {
            if (value.is_null())
            {
                return object_value{};
            }
            const auto type_it = value.is_object() ? value.find("type") : value.end();
            if (!value.is_object() || type_it == value.end() || !type_it->is_string())
            {
                problem = "is not null or an object with a \"type\"";
                return std::nullopt;
            }
            const std::string type_name = type_it->get<std::string>();
            const type_info* type = registry.find(type_name);
            if (type == nullptr || type->category != type_category::object)
            {
                problem = "names '" + type_name + "', which is not a registered object type";
                return std::nullopt;
            }

            object_value out;
            out.type = type_name;
            const auto fields_it = value.find("fields");
            if (fields_it == value.end() || !fields_it->is_object())
            {
                return out;
            }
            for (const field_info& field : type->fields)
            {
                const auto entry = fields_it->find(field.name);
                if (entry == fields_it->end())
                {
                    continue;
                }
                std::string inner;
                std::optional<field_value> decoded = decode_value(registry, field, *entry, inner);
                if (!decoded.has_value())
                {
                    problem = "field '" + field.name + "' " + inner;
                    return std::nullopt;
                }
                out.fields.push_back(field_entry{field.name, std::move(*decoded)});
            }
            return out;
        }

        std::optional<field_value>
        decode_value(const type_registry& registry, const field_info& field, const json& value, std::string& problem)
        {
            switch (field.kind)
            {
            case field_kind::boolean:
                if (value.is_boolean())
                {
                    return field_value{value.get<bool>()};
                }
                problem = "is not a boolean";
                return std::nullopt;
            case field_kind::integer:
                if (value.is_number_integer())
                {
                    return field_value{value.get<int64_t>()};
                }
                problem = "is not an integer";
                return std::nullopt;
            case field_kind::number:
                if (value.is_number())
                {
                    return field_value{static_cast<float>(value.get<double>())};
                }
                problem = "is not a number";
                return std::nullopt;
            case field_kind::vec2:
                if (const auto numbers = read_numbers(value, 2))
                {
                    return field_value{core::math::vec2{(*numbers)[0], (*numbers)[1]}};
                }
                problem = "is not an array of 2 numbers";
                return std::nullopt;
            case field_kind::vec3:
                if (const auto numbers = read_numbers(value, 3))
                {
                    return field_value{core::math::vec3{(*numbers)[0], (*numbers)[1], (*numbers)[2]}};
                }
                problem = "is not an array of 3 numbers";
                return std::nullopt;
            case field_kind::vec4:
                if (const auto numbers = read_numbers(value, 4))
                {
                    return field_value{core::math::vec4{(*numbers)[0], (*numbers)[1], (*numbers)[2], (*numbers)[3]}};
                }
                problem = "is not an array of 4 numbers";
                return std::nullopt;
            case field_kind::quat:
                if (const auto numbers = read_numbers(value, 4))
                {
                    return field_value{core::math::quat{(*numbers)[0], (*numbers)[1], (*numbers)[2], (*numbers)[3]}};
                }
                problem = "is not an array of 4 numbers (w, x, y, z)";
                return std::nullopt;
            case field_kind::color:
                if (const auto rgb = read_numbers(value, 3))
                {
                    return field_value{core::math::vec3{(*rgb)[0], (*rgb)[1], (*rgb)[2]}};
                }
                if (const auto rgba = read_numbers(value, 4))
                {
                    return field_value{core::math::vec4{(*rgba)[0], (*rgba)[1], (*rgba)[2], (*rgba)[3]}};
                }
                problem = "is not an array of 3 or 4 numbers";
                return std::nullopt;
            case field_kind::string:
            case field_kind::string_id:
            case field_kind::asset_ref:
                if (value.is_string())
                {
                    return field_value{value.get<std::string>()};
                }
                problem = "is not a string";
                return std::nullopt;
            case field_kind::enumeration:
                if (value.is_string())
                {
                    const std::string name = value.get<std::string>();
                    if (const enum_entry* named = field.find_enumerator(name))
                    {
                        return field_value{named->value};
                    }
                    problem = "names '" + name + "', which is not one of its values";
                    return std::nullopt;
                }
                if (value.is_number_integer() &&
                    (field.enumerators.empty() || field.find_enumerator(value.get<int64_t>()) != nullptr))
                {
                    return field_value{value.get<int64_t>()};
                }
                problem = "is not one of its value names";
                return std::nullopt;
            case field_kind::object:
                if (auto object = decode_object(registry, value, problem))
                {
                    return field_value{std::move(*object)};
                }
                return std::nullopt;
            }
            problem = "has an unknown kind";
            return std::nullopt;
        }

        // -- Printing -----------------------------------------------------------

        std::string print_scalar(const json& value)
        {
            return value.dump(-1, ' ', false, json::error_handler_t::replace);
        }

        // Indented like dump(4), except that an array of scalars (a vector, a
        // colour) stays on one line.
        void print(const json& value, std::string& out, std::size_t indent)
        {
            if (value.is_object())
            {
                if (value.empty())
                {
                    out += "{}";
                    return;
                }
                out += "{\n";
                std::size_t index = 0;
                for (auto it = value.begin(); it != value.end(); ++it, ++index)
                {
                    out.append(indent + 4, ' ');
                    out += print_scalar(json(it.key()));
                    out += ": ";
                    print(it.value(), out, indent + 4);
                    out += index + 1 < value.size() ? ",\n" : "\n";
                }
                out.append(indent, ' ');
                out += '}';
                return;
            }
            if (value.is_array())
            {
                if (value.empty())
                {
                    out += "[]";
                    return;
                }
                const bool flat = std::none_of(
                    value.begin(), value.end(), [](const json& element) { return element.is_structured(); });
                if (flat)
                {
                    out += '[';
                    for (std::size_t i = 0; i < value.size(); ++i)
                    {
                        out += i == 0 ? "" : ", ";
                        out += print_scalar(value[i]);
                    }
                    out += ']';
                    return;
                }
                out += "[\n";
                for (std::size_t i = 0; i < value.size(); ++i)
                {
                    out.append(indent + 4, ' ');
                    print(value[i], out, indent + 4);
                    out += i + 1 < value.size() ? ",\n" : "\n";
                }
                out.append(indent, ' ');
                out += ']';
                return;
            }
            out += print_scalar(value);
        }

        std::string node_label(const node& target)
        {
            return target.name().empty() ? std::string{"<unnamed>"} : std::string{target.name().view()};
        }

        // -- Saving -------------------------------------------------------------

        // One problem a save met, however many nodes it met it on.
        struct save_note
        {
            std::string message;
            std::string first_node;
            std::size_t count{0};
        };

        struct save_state
        {
            const type_registry& registry;
            json nodes = json::array();
            int64_t next_id{1};
            std::vector<save_note> notes{};
        };

        // Records @p message against @p owner; finish() logs each distinct
        // message once, with how many nodes it concerned.
        void note(save_state& state, const node& owner, std::string message)
        {
            for (save_note& existing : state.notes)
            {
                if (existing.message == message)
                {
                    ++existing.count;
                    return;
                }
            }
            state.notes.push_back(save_note{std::move(message), node_label(owner), 1});
        }

        json placeholder_entry(const std::string& type_name, const std::string& reason)
        {
            json entry = json::object();
            if (!type_name.empty())
            {
                entry["type"] = type_name;
            }
            entry["placeholder"] = reason;
            return entry;
        }

        json save_registered(save_state& state, const node& owner, const type_info& type, const void* object)
        {
            const std::string reason = type.placeholder_reason ? type.placeholder_reason(object) : std::string{};
            if (!reason.empty())
            {
                note(state, owner, type.name + " saved as a placeholder: " + reason);
                return placeholder_entry(type.name, reason);
            }
            if (type.lossy_reason)
            {
                const std::string lossy = type.lossy_reason(object);
                if (!lossy.empty())
                {
                    note(state, owner, type.name + ": " + lossy);
                }
            }

            json fields = json::object();
            for (const field_info& field : type.fields)
            {
                if (field.applies_to(object))
                {
                    fields[field.name] = encode_value(state.registry, field.get(object), &field);
                }
            }
            json entry = json::object();
            entry["type"] = type.name;
            entry["fields"] = std::move(fields);
            return entry;
        }

        json save_behavior(save_state& state, node& owner)
        {
            const behavior_component* holder = owner.get_component<behavior_component>();
            const behavior* logic = holder != nullptr ? holder->get() : nullptr;
            if (logic == nullptr)
            {
                return json(nullptr);
            }
            const type_info* type = state.registry.find(std::type_index{typeid(*logic)});
            if (type == nullptr || type->category != type_category::behavior)
            {
                note(state,
                     owner,
                     std::string{"a behaviour of type "} + typeid(*logic).name() +
                         " has no registered name; saved as a placeholder");
                return placeholder_entry({}, "a behaviour of a type with no registered name");
            }
            return save_registered(state, owner, *type, dynamic_cast<const void*>(logic));
        }

        json save_components(save_state& state, node& owner)
        {
            const std::type_index placeholder_type{typeid(placeholder_component)};
            const std::type_index behavior_type{typeid(behavior_component)};

            json entries = json::array();
            for (const std::type_index type : owner.component_types())
            {
                if (type == placeholder_type)
                {
                    continue;
                }
                if (type == behavior_type)
                {
                    // An empty behavior_component carries nothing to save.
                    json entry = save_behavior(state, owner);
                    if (!entry.is_null())
                    {
                        entries.push_back(std::move(entry));
                    }
                    continue;
                }
                const type_info* info = state.registry.find(type);
                if (info == nullptr || info->category != type_category::component)
                {
                    note(state,
                         owner,
                         std::string{"a component of type "} + type.name() +
                             " has no registered name; saved as a placeholder");
                    entries.push_back(placeholder_entry({}, "a component of a type with no registered name"));
                    continue;
                }
                entries.push_back(save_registered(state, owner, *info, info->find(owner)));
            }

            // The entries a load parked go back where they were in the file.
            // They are in file order, so inserting each at its position
            // interleaves them with the rebuilt components exactly as before.
            if (const placeholder_component* parked = owner.get_component<placeholder_component>())
            {
                for (const placeholder_component::entry& held : parked->entries)
                {
                    json entry = json::parse(held.text, nullptr, false);
                    if (entry.is_discarded())
                    {
                        continue;
                    }
                    const std::size_t position = std::min(held.position, entries.size());
                    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(position), std::move(entry));
                }
            }
            return entries;
        }

        void save_node(save_state& state, node& target, const json& parent_id)
        {
            if (target.is_destroy_pending())
            {
                return;
            }
            const int64_t id = state.next_id++;

            const core::math::vec3 position = target.transform.get_position();
            const core::math::quat rotation = target.transform.get_quaternion();
            const core::math::vec3 scale = target.transform.get_scale();
            json transform = json::object();
            transform["position"] = number_array({position.x, position.y, position.z});
            transform["rotation"] = number_array({rotation.w, rotation.x, rotation.y, rotation.z});
            transform["scale"] = number_array({scale.x, scale.y, scale.z});

            json entry = json::object();
            entry["id"] = id;
            entry["name"] = std::string{target.name().view()};
            entry["parent"] = parent_id;
            entry["active"] = target.is_active();
            entry["transform"] = std::move(transform);
            entry["components"] = save_components(state, target);
            state.nodes.push_back(std::move(entry));

            for (node* child : target.children())
            {
                if (child != nullptr)
                {
                    save_node(state, *child, json(id));
                }
            }
        }

        scene_document finish(save_state& state)
        {
            for (const save_note& problem : state.notes)
            {
                if (problem.count == 1)
                {
                    LOG_WRN("scene save: node '%s': %s", problem.first_node.c_str(), problem.message.c_str());
                }
                else
                {
                    LOG_WRN("scene save: node '%s' and %zu more: %s",
                            problem.first_node.c_str(),
                            problem.count - 1,
                            problem.message.c_str());
                }
            }

            json document = json::object();
            document["format"] = k_format;
            document["version"] = k_version;
            document["nodes"] = std::move(state.nodes);
            return scene_io::make(std::move(document));
        }

        // -- Loading ------------------------------------------------------------

        struct load_state
        {
            const type_registry& registry;
            std::size_t kept{0};
        };

        // Parks @p entry on @p owner so the next save writes it back.
        void park(load_state& state, node& owner, std::size_t position, const json& entry)
        {
            placeholder_component* parked = owner.get_component<placeholder_component>();
            if (parked == nullptr)
            {
                parked = owner.add_component(placeholder_component{});
            }
            if (parked == nullptr)
            {
                return;
            }
            parked->entries.push_back(
                placeholder_component::entry{position, entry.dump(-1, ' ', false, json::error_handler_t::replace)});
            ++state.kept;
        }

        // Applies the registered fields present in @p fields to @p object, in
        // the type's field order. A field of the wrong shape (or one that does
        // not apply) is skipped with a warning; false when a setter refused
        // its value, which leaves the object unfit to attach.
        bool apply_fields(
            load_state& state, const std::string& where, const type_info& type, void* object, const json& fields)
        {
            bool applied = true;
            for (const field_info& field : type.fields)
            {
                const auto it = fields.find(field.name);
                if (it == fields.end())
                {
                    continue;
                }
                if (!field.applies_to(object))
                {
                    LOG_WRN("%s: field '%s' does not apply here; ignored", where.c_str(), field.name.c_str());
                    continue;
                }
                std::string problem;
                std::optional<field_value> decoded = decode_value(state.registry, field, *it, problem);
                if (!decoded.has_value())
                {
                    LOG_WRN("%s: field '%s' %s; ignored", where.c_str(), field.name.c_str(), problem.c_str());
                    continue;
                }
                if (!field.set(object, *decoded))
                {
                    LOG_WRN("%s: field '%s' could not be applied", where.c_str(), field.name.c_str());
                    applied = false;
                }
            }
            for (auto it = fields.begin(); it != fields.end(); ++it)
            {
                if (type.find_field(it.key()) == nullptr)
                {
                    LOG_WRN("%s: unknown field '%s' ignored", where.c_str(), it.key().c_str());
                }
            }
            return applied;
        }

        void load_component(load_state& state, node& owner, std::size_t position, const json& entry)
        {
            const std::string label = "scene load: node '" + node_label(owner) + "'";
            if (!entry.is_object())
            {
                LOG_WRN("%s: component entry %zu is not an object; kept as a placeholder", label.c_str(), position);
                park(state, owner, position, entry);
                return;
            }
            const auto type_it = entry.find("type");
            if (entry.contains("placeholder") || type_it == entry.end() || !type_it->is_string())
            {
                // Saved as a placeholder: it stays one.
                park(state, owner, position, entry);
                return;
            }

            const std::string type_name = type_it->get<std::string>();
            const type_info* type = state.registry.find(type_name);
            if (type == nullptr || type->category == type_category::object)
            {
                LOG_WRN("%s: no component or behaviour is registered as '%s'; kept as a placeholder",
                        label.c_str(),
                        type_name.c_str());
                park(state, owner, position, entry);
                return;
            }

            const std::string where = label + ": " + type_name;
            static const json no_fields = json::object();
            const auto fields_it = entry.find("fields");
            if (fields_it != entry.end() && !fields_it->is_object())
            {
                LOG_WRN("%s: \"fields\" is not an object; ignored", where.c_str());
            }
            const json& fields = fields_it != entry.end() && fields_it->is_object() ? *fields_it : no_fields;

            // The fields are applied before the component attaches, so it
            // registers with the renderer (or the camera / light registry)
            // already configured, and one that turns out unfit is dropped
            // without ever having been attached.
            std::string refused;
            auto configure = [&](void* object)
            {
                if (!apply_fields(state, where, *type, object, fields))
                {
                    refused = "a field could not be applied";
                    return false;
                }
                if (type->placeholder_reason)
                {
                    refused = type->placeholder_reason(object);
                }
                return refused.empty();
            };

            bool attached = false;
            if (type->category == type_category::behavior)
            {
                if (owner.has_component<behavior_component>())
                {
                    LOG_WRN("%s: the node already has a behaviour; this one replaces it", where.c_str());
                }
                std::unique_ptr<behavior> logic = type->create_behavior ? type->create_behavior() : nullptr;
                if (logic != nullptr && configure(dynamic_cast<void*>(logic.get())))
                {
                    attached = owner.add_component(behavior_component{std::move(logic)}) != nullptr;
                }
            }
            else
            {
                if (type->find(owner) != nullptr)
                {
                    LOG_WRN("%s: the node already has one; this one replaces it", where.c_str());
                }
                attached = type->attach(owner, configure);
            }

            if (!attached)
            {
                LOG_WRN("%s: not restored (%s); kept as a placeholder",
                        where.c_str(),
                        refused.empty() ? "the node did not take it" : refused.c_str());
                park(state, owner, position, entry);
            }
        }

        void apply_transform(node& target, const json& entry)
        {
            const auto it = entry.find("transform");
            if (it == entry.end())
            {
                return;
            }
            if (!it->is_object())
            {
                LOG_WRN("scene load: node '%s': \"transform\" is not an object; ignored", node_label(target).c_str());
                return;
            }
            const json& transform = *it;
            auto numbers = [&](const char* key, std::size_t count) -> std::optional<std::array<float, 4>>
            {
                const auto field = transform.find(key);
                if (field == transform.end())
                {
                    return std::nullopt;
                }
                auto read = read_numbers(*field, count);
                if (!read.has_value())
                {
                    LOG_WRN("scene load: node '%s': transform %s is not an array of %zu numbers; ignored",
                            node_label(target).c_str(),
                            key,
                            count);
                }
                return read;
            };

            if (const auto position = numbers("position", 3))
            {
                target.transform.set_position(core::math::vec3{(*position)[0], (*position)[1], (*position)[2]});
            }
            if (const auto rotation = numbers("rotation", 4))
            {
                const core::math::quat q{(*rotation)[0], (*rotation)[1], (*rotation)[2], (*rotation)[3]};
                const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
                // A saved rotation is restored bit for bit (normalising it
                // again could move its last bit); a hand-written one that is
                // not unit length is normalised.
                if (std::abs(length - 1.0f) <= k_unit_tolerance)
                {
                    target.transform.set_quaternion_exact(q);
                }
                else
                {
                    target.transform.set_quaternion(q);
                }
            }
            if (const auto scale = numbers("scale", 3))
            {
                target.transform.set_scale(core::math::vec3{(*scale)[0], (*scale)[1], (*scale)[2]});
            }
        }

        std::string describe_id(const json& value)
        {
            return value.is_primitive() ? print_scalar(value) : std::string{"?"};
        }

        // The scene a file loads as: its file name up to the first dot.
        std::string scene_name_for(const std::filesystem::path& path)
        {
            const std::string file = core::platform::path_to_utf8(path.filename());
            const std::size_t dot = file.find('.');
            return dot == std::string::npos || dot == 0 ? file : file.substr(0, dot);
        }
    } // namespace

    scene_document::scene_document()
    {
        json document = json::object();
        document["format"] = k_format;
        document["version"] = k_version;
        document["nodes"] = json::array();
        auto data = std::make_shared<contents>();
        data->document = std::move(document);
        m_data = std::move(data);
    }

    scene_document::scene_document(std::shared_ptr<const contents> data) : m_data{std::move(data)} {}

    std::optional<scene_document> scene_document::parse(std::string_view text, std::string* error)
    {
        auto fail = [&](std::string reason) -> std::optional<scene_document>
        {
            if (error != nullptr)
            {
                *error = std::move(reason);
            }
            return std::nullopt;
        };

        json document = json::parse(text, nullptr, false);
        if (document.is_discarded())
        {
            return fail("not valid JSON");
        }
        if (!document.is_object())
        {
            return fail("not a JSON object");
        }
        const auto format = document.find("format");
        if (format == document.end() || !format->is_string() || format->get<std::string>() != k_format)
        {
            return fail(std::string{"\"format\" is not \""} + k_format + "\"");
        }
        const auto version = document.find("version");
        if (version == document.end() || !version->is_number_integer())
        {
            return fail("\"version\" is missing or not an integer");
        }
        if (version->get<int64_t>() > k_version || version->get<int64_t>() < 1)
        {
            return fail("version " + std::to_string(version->get<int64_t>()) + " is not one this build reads (1 to " +
                        std::to_string(k_version) + ")");
        }
        const auto nodes = document.find("nodes");
        if (nodes == document.end() || !nodes->is_array())
        {
            return fail("\"nodes\" is missing or not an array");
        }
        return scene_io::make(std::move(document));
    }

    std::optional<scene_document> scene_document::read(const std::filesystem::path& path, std::string* error)
    {
        std::string text;
        std::string reason;
        if (!core::default_vfs().read_text_file(path, text, &reason))
        {
            if (error != nullptr)
            {
                *error = reason;
            }
            return std::nullopt;
        }
        return parse(text, error);
    }

    std::string scene_document::to_string() const
    {
        std::string out;
        print(m_data->document, out, 0);
        out += '\n';
        return out;
    }

    bool scene_document::write(const std::filesystem::path& path, std::string* error) const
    {
        return core::default_vfs().write_text_file(path, to_string(), error);
    }

    std::size_t scene_document::node_count() const noexcept
    {
        const auto nodes = m_data->document.find("nodes");
        return nodes != m_data->document.end() && nodes->is_array() ? nodes->size() : 0;
    }

    prefab_document save_subtree(node& root)
    {
        save_state state{default_type_registry()};
        save_node(state, root, json(nullptr));
        return finish(state);
    }

    scene_document save_scene(context& scene)
    {
        save_state state{default_type_registry()};
        for (node* child : scene.root.children())
        {
            if (child != nullptr)
            {
                save_node(state, *child, json(nullptr));
            }
        }
        return finish(state);
    }

    bool save_scene(context& scene, const std::filesystem::path& path)
    {
        const scene_document document = save_scene(scene);
        std::string error;
        if (!document.write(path, &error))
        {
            LOG_ERR("scene save: could not write %s: %s", core::platform::path_to_utf8(path).c_str(), error.c_str());
            return false;
        }
        LOG_INF("Saved scene %s (%zu nodes)", core::platform::path_to_utf8(path).c_str(), document.node_count());
        return true;
    }

    std::vector<node*> instantiate(const scene_document& document, node& parent)
    {
        std::vector<node*> roots;
        context* scene = parent.scene();
        if (scene == nullptr)
        {
            LOG_ERR("scene load: the parent node belongs to no scene; nothing instantiated");
            return roots;
        }
        if (scene->is_traversing())
        {
            LOG_ERR("scene load: the parent's scene is mid-update; instantiate from outside its update");
            return roots;
        }

        load_session session;
        load_state state{default_type_registry()};
        std::unordered_map<int64_t, node*> by_id;

        const json& contents = scene_io::contents_of(document);
        const auto nodes = contents.find("nodes");
        if (nodes == contents.end() || !nodes->is_array())
        {
            return roots;
        }

        for (const json& entry : *nodes)
        {
            if (!entry.is_object())
            {
                LOG_WRN("scene load: a node entry is not an object; skipped");
                continue;
            }

            // The format lists every parent before its children.
            node* target_parent = &parent;
            const auto parent_it = entry.find("parent");
            if (parent_it != entry.end() && !parent_it->is_null())
            {
                const auto found = parent_it->is_number_integer() ? by_id.find(parent_it->get<int64_t>()) : by_id.end();
                if (found != by_id.end())
                {
                    target_parent = found->second;
                }
                else
                {
                    LOG_WRN(
                        "scene load: node %s names parent %s, which is not listed before it; placed at the top level",
                        describe_id(entry.value("id", json(nullptr))).c_str(),
                        describe_id(*parent_it).c_str());
                }
            }

            const auto name_it = entry.find("name");
            const std::string name = name_it != entry.end() && name_it->is_string() ? name_it->get<std::string>() : "";
            node& created = scene->create_node(core::string_id{name}, target_parent);

            const auto id_it = entry.find("id");
            if (id_it != entry.end() && id_it->is_number_integer())
            {
                if (!by_id.emplace(id_it->get<int64_t>(), &created).second)
                {
                    LOG_WRN("scene load: node id %s is used twice; children name the first",
                            describe_id(*id_it).c_str());
                }
            }

            apply_transform(created, entry);

            const auto components = entry.find("components");
            if (components != entry.end() && components->is_array())
            {
                for (std::size_t position = 0; position < components->size(); ++position)
                {
                    load_component(state, created, position, (*components)[position]);
                }
            }

            // After the components, so a disabled node hides them through
            // the usual on_active_changed (as context::clone does it).
            const auto active = entry.find("active");
            if (active != entry.end() && active->is_boolean() && !active->get<bool>())
            {
                created.set_active(false);
            }

            if (target_parent == &parent)
            {
                roots.push_back(&created);
            }
        }

        if (state.kept > 0)
        {
            LOG_WRN("scene load: %zu component entr%s could not be restored and %s kept as placeholders",
                    state.kept,
                    state.kept == 1 ? "y" : "ies",
                    state.kept == 1 ? "is" : "are");
        }
        return roots;
    }

    context* load_scene(scene_manager& scenes, const std::filesystem::path& path, bool additive)
    {
        const std::string label = core::platform::path_to_utf8(path);
        std::string error;
        const std::optional<scene_document> document = scene_document::read(path, &error);
        if (!document.has_value())
        {
            LOG_ERR("scene load: could not load %s: %s", label.c_str(), error.c_str());
            return nullptr;
        }

        const core::string_id name{scene_name_for(path)};
        if (context* existing = scenes.find(name))
        {
            if (existing == &scenes.persistent_scene() || existing->is_traversing())
            {
                LOG_ERR("scene load: cannot replace scene '%s' %s",
                        name.c_str(),
                        existing->is_traversing() ? "while it is updating" : "(it is the persistent scene)");
                return nullptr;
            }
            scenes.unload(*existing);
            if (scenes.find(name) != nullptr)
            {
                LOG_ERR("scene load: scene '%s' could not be unloaded to be replaced (the scenes are updating)",
                        name.c_str());
                return nullptr;
            }
        }

        context& scene = scenes.load(name, additive ? load_mode::additive : load_mode::single);
        instantiate(*document, scene.root);
        LOG_INF("Loaded %zu nodes into scene '%s' from %s", document->node_count(), name.c_str(), label.c_str());
        return &scene;
    }

    void keep_alive_while_loading(std::shared_ptr<const void> resource)
    {
        if (load_session* session = load_session::current())
        {
            session->held.push_back(std::move(resource));
        }
    }
} // namespace runtime
