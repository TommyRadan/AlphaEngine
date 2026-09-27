// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/settings_registry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <core/log.hpp>
#include <core/settings_parse.hpp>

namespace core
{
    namespace
    {
        using json = nlohmann::json;

        std::string dotted(const settings_section& section, const settings_field& field)
        {
            return section.name + "." + field.key;
        }

        // -- JSON --------------------------------------------------------------

        void apply_field_json(const settings_section& section, const settings_field& field, const json& value)
        {
            const std::string key = dotted(section, field);
            void* instance = section.instance;
            switch (field.kind)
            {
            case settings_value_kind::flag:
            {
                bool& target = field.flag_ref(instance);
                if (!value.is_boolean())
                {
                    LOG_WRN("settings: %s must be true or false; keeping %s", key.c_str(), target ? "true" : "false");
                    return;
                }
                target = value.get<bool>();
                return;
            }
            case settings_value_kind::count:
            {
                unsigned int& target = field.count_ref(instance);
                if (!value.is_number_unsigned())
                {
                    LOG_WRN("settings: %s must be an unsigned integer; keeping %u", key.c_str(), target);
                    return;
                }
                const auto raw = value.get<std::uint64_t>();
                if (raw < field.count_min || raw > field.count_max)
                {
                    LOG_WRN("settings: %s=%llu is outside [%u, %u]; keeping %u",
                            key.c_str(),
                            static_cast<unsigned long long>(raw),
                            field.count_min,
                            field.count_max,
                            target);
                    return;
                }
                target = static_cast<unsigned int>(raw);
                return;
            }
            case settings_value_kind::number:
            {
                float& target = field.number_ref(instance);
                if (!value.is_number())
                {
                    LOG_WRN("settings: %s must be a number; keeping %g", key.c_str(), static_cast<double>(target));
                    return;
                }
                const double raw = value.get<double>();
                if (!std::isfinite(raw) || raw < static_cast<double>(field.number_min) ||
                    raw > static_cast<double>(field.number_max))
                {
                    LOG_WRN("settings: %s=%g is outside [%g, %g]; keeping %g",
                            key.c_str(),
                            raw,
                            static_cast<double>(field.number_min),
                            static_cast<double>(field.number_max),
                            static_cast<double>(target));
                    return;
                }
                target = static_cast<float>(raw);
                return;
            }
            case settings_value_kind::text:
            {
                std::string& target = field.text_ref(instance);
                if (!value.is_string())
                {
                    LOG_WRN("settings: %s must be a string; keeping '%s'", key.c_str(), target.c_str());
                    return;
                }
                target = value.get<std::string>();
                return;
            }
            case settings_value_kind::choice:
            {
                if (!value.is_string())
                {
                    LOG_WRN(
                        "settings: %s must be a string; keeping %s", key.c_str(), field.choice_get(instance).c_str());
                    return;
                }
                const std::string text = value.get<std::string>();
                if (!field.choice_validate(text))
                {
                    LOG_WRN("settings: %s='%s' is not %s; keeping %s",
                            key.c_str(),
                            text.c_str(),
                            field.choice_phrase.c_str(),
                            field.choice_get(instance).c_str());
                    return;
                }
                field.choice_apply(instance, text);
                return;
            }
            }
        }

        // -- Text (environment / command line) ----------------------------------

        // Pure: parses `text` for `field`, warning under `source` (an environment variable or command-line
        // flag name) when it does not parse. Never touches any settings instance.
        std::optional<std::string>
        validate_field_text(const settings_field& field, const char* source, std::string_view text)
        {
            switch (field.kind)
            {
            case settings_value_kind::flag:
            {
                if (!parse_bool(text).has_value())
                {
                    LOG_WRN("settings: %s='%s' is not a boolean (true/false, 1/0, yes/no, on/off); ignoring it",
                            source,
                            std::string{text}.c_str());
                    return std::nullopt;
                }
                return std::string{trim(text)};
            }
            case settings_value_kind::count:
            {
                if (!parse_unsigned(text, field.count_min, field.count_max).has_value())
                {
                    LOG_WRN("settings: %s='%s' is not an integer in [%u, %u]; ignoring it",
                            source,
                            std::string{text}.c_str(),
                            field.count_min,
                            field.count_max);
                    return std::nullopt;
                }
                return std::string{trim(text)};
            }
            case settings_value_kind::number:
            {
                if (!parse_float(text, field.number_min, field.number_max).has_value())
                {
                    LOG_WRN("settings: %s='%s' is not a number in [%g, %g]; ignoring it",
                            source,
                            std::string{text}.c_str(),
                            static_cast<double>(field.number_min),
                            static_cast<double>(field.number_max));
                    return std::nullopt;
                }
                return std::string{trim(text)};
            }
            case settings_value_kind::text:
                return std::string{trim(text)};
            case settings_value_kind::choice:
            {
                std::string trimmed{trim(text)};
                if (!field.choice_validate(trimmed))
                {
                    LOG_WRN("settings: %s='%s' is not %s; ignoring it",
                            source,
                            std::string{text}.c_str(),
                            field.choice_phrase.c_str());
                    return std::nullopt;
                }
                return trimmed;
            }
            }
            return std::nullopt;
        }

        // Assumes `text` already passed validate_field_text for this field; writes it in.
        void assign_field_text(const settings_field& field, void* instance, const std::string& text)
        {
            switch (field.kind)
            {
            case settings_value_kind::flag:
                field.flag_ref(instance) = *parse_bool(text);
                return;
            case settings_value_kind::count:
                field.count_ref(instance) = *parse_unsigned(text, field.count_min, field.count_max);
                return;
            case settings_value_kind::number:
                field.number_ref(instance) = *parse_float(text, field.number_min, field.number_max);
                return;
            case settings_value_kind::text:
                field.text_ref(instance) = text;
                return;
            case settings_value_kind::choice:
                field.choice_apply(instance, text);
                return;
            }
        }

        bool looks_like_option(const char* arg)
        {
            return arg != nullptr && std::string_view{arg}.starts_with("--");
        }

        // What matched a `--name` token: a field taking a generic value, an exclusive choice flag already
        // resolved to its named value, or a presence-only flag. `nullopt` fields mean "no match".
        struct cli_match
        {
            std::size_t section_index{0};
            std::size_t field_index{0};
            // Set only for an exclusive choice flag, whose value is the flag itself rather than parsed text.
            std::optional<std::string> resolved_choice_value;
        };

        std::optional<cli_match> find_cli_match(const std::vector<std::unique_ptr<settings_section>>& sections,
                                                std::string_view name)
        {
            for (std::size_t section_index = 0; section_index < sections.size(); ++section_index)
            {
                const settings_section& section = *sections[section_index];
                for (std::size_t field_index = 0; field_index < section.fields.size(); ++field_index)
                {
                    const settings_field& field = section.fields[field_index];
                    for (const settings_choice_flag& choice_flag : field.choice_flags)
                    {
                        if (choice_flag.flag == name)
                        {
                            return cli_match{section_index, field_index, choice_flag.value_name};
                        }
                    }
                    if (field.cli_style != settings_cli_style::none && field.cli_flag == name)
                    {
                        return cli_match{section_index, field_index, std::nullopt};
                    }
                }
            }
            return std::nullopt;
        }

        // Records `text` for (section_index, field_index) in `values`, overwriting any earlier occurrence so
        // the last valid one wins.
        void record_resolved_value(std::vector<settings_command_line_result::resolved_value>& values,
                                   std::size_t section_index,
                                   std::size_t field_index,
                                   std::string text)
        {
            const auto existing = std::find_if(
                values.begin(),
                values.end(),
                [&](const auto& v) { return v.section_index == section_index && v.field_index == field_index; });
            if (existing == values.end())
            {
                values.push_back({section_index, field_index, std::move(text)});
            }
            else
            {
                existing->text = std::move(text);
            }
        }
    } // namespace

    const char* on_off(bool value) noexcept
    {
        return value ? "on" : "off";
    }

    settings_field& settings_section::add_field(settings_field field)
    {
        fields.push_back(std::move(field));
        return fields.back();
    }

    settings_section& settings_registry::add_section(std::string name, void* instance)
    {
        m_sections.push_back(std::make_unique<settings_section>(std::move(name), instance));
        return *m_sections.back();
    }

    bool settings_registry::apply_json(std::string_view text) const
    {
        // Exceptions off: a bad document comes back as a discarded value.
        const json document = json::parse(text.begin(), text.end(), nullptr, false);
        if (document.is_discarded())
        {
            LOG_WRN("settings: the settings file is not valid JSON; ignoring it");
            return false;
        }
        if (!document.is_object())
        {
            LOG_WRN("settings: the settings file must hold a JSON object; ignoring it");
            return false;
        }

        for (const auto& [section_name, section_json] : document.items())
        {
            if (!section_json.is_object())
            {
                LOG_WRN("settings: '%s' must be a JSON object; ignoring it", section_name.c_str());
                continue;
            }
            const auto section_it = std::find_if(
                m_sections.begin(), m_sections.end(), [&](const auto& s) { return s->name == section_name; });
            if (section_it == m_sections.end())
            {
                LOG_WRN("settings: ignoring unknown section '%s'", section_name.c_str());
                continue;
            }
            const settings_section& section = **section_it;
            for (const auto& [key, value] : section_json.items())
            {
                const auto field_it = std::find_if(section.fields.begin(),
                                                   section.fields.end(),
                                                   [&](const settings_field& f) { return f.key == key; });
                if (field_it != section.fields.end())
                {
                    apply_field_json(section, *field_it, value);
                }
                else if (section.custom_json)
                {
                    section.custom_json(section.instance, key, value);
                }
                else
                {
                    LOG_WRN("settings: ignoring unknown key '%s.%s'", section_name.c_str(), key.c_str());
                }
            }
        }
        return true;
    }

    void settings_registry::apply_environment(const environment_getter& get) const
    {
        const auto read = [&get](const char* name) -> std::optional<std::string_view>
        {
            const char* value = get ? get(name) : nullptr;
            if (value == nullptr || trim(value).empty())
            {
                return std::nullopt;
            }
            return std::string_view{value};
        };

        for (const auto& section : m_sections)
        {
            for (const settings_field& field : section->fields)
            {
                if (!field.env_name.has_value())
                {
                    continue;
                }
                const auto text = read(field.env_name->c_str());
                if (!text.has_value())
                {
                    continue;
                }
                if (const auto validated = validate_field_text(field, field.env_name->c_str(), *text))
                {
                    assign_field_text(field, section->instance, *validated);
                }
            }
        }
    }

    settings_command_line_result settings_registry::parse_command_line(std::span<const char* const> args) const
    {
        settings_command_line_result out;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == nullptr)
            {
                continue;
            }
            const std::string_view arg{args[i]};
            if (arg == "--help" || arg == "-h")
            {
                out.help_requested = true;
                continue;
            }
            if (!arg.starts_with("--") || arg.size() == 2)
            {
                LOG_WRN("settings: ignoring unexpected argument '%s'", std::string{arg}.c_str());
                continue;
            }

            // --key=value splits here; --key value picks its value up below.
            const std::size_t equals = arg.find('=');
            const std::string name{arg.substr(0, equals)};
            std::optional<std::string_view> value;
            if (equals != std::string_view::npos)
            {
                value = arg.substr(equals + 1);
            }

            const auto take_value = [&]() -> bool
            {
                if (value.has_value())
                {
                    return true;
                }
                if (i + 1 < args.size() && args[i + 1] != nullptr && !looks_like_option(args[i + 1]))
                {
                    ++i;
                    value = std::string_view{args[i]};
                    return true;
                }
                return false;
            };

            // Core-owned meta options: which file to read and the log level. Neither is backed by a
            // registered field, so they are captured directly rather than recorded for apply_command_line.
            if (name == "--settings" || name == "--log-level")
            {
                if (!take_value() || trim(*value).empty())
                {
                    LOG_WRN("settings: %s needs a value; ignoring it", name.c_str());
                    continue;
                }
                std::string trimmed{trim(*value)};
                if (name == "--settings")
                {
                    out.settings_path = std::move(trimmed);
                }
                else
                {
                    out.log_level = std::move(trimmed);
                }
                continue;
            }

            const auto match = find_cli_match(m_sections, name);
            if (!match.has_value())
            {
                LOG_WRN("settings: ignoring unknown option '%s'", name.c_str());
                continue;
            }
            const settings_field& field = m_sections[match->section_index]->fields[match->field_index];

            if (match->resolved_choice_value.has_value() || field.cli_style == settings_cli_style::presence)
            {
                // An exclusive choice flag (--windowed) or a presence-only flag (--fail-on-error): neither
                // takes a value.
                if (value.has_value())
                {
                    LOG_WRN("settings: %s takes no value; ignoring it", name.c_str());
                    continue;
                }
                record_resolved_value(out.values,
                                      match->section_index,
                                      match->field_index,
                                      match->resolved_choice_value.value_or("true"));
                continue;
            }

            // A generic `--flag <value>` option.
            if (!take_value() || trim(*value).empty())
            {
                LOG_WRN("settings: %s needs a value; ignoring it", name.c_str());
                continue;
            }
            if (const auto validated = validate_field_text(field, name.c_str(), *value))
            {
                record_resolved_value(out.values, match->section_index, match->field_index, *validated);
            }
        }
        return out;
    }

    void settings_registry::apply_command_line(const settings_command_line_result& result) const
    {
        for (const auto& resolved : result.values)
        {
            const settings_section& section = *m_sections[resolved.section_index];
            const settings_field& field = section.fields[resolved.field_index];
            assign_field_text(field, section.instance, resolved.text);
        }
    }

    std::string settings_registry::help_lines_for(std::string_view section_name) const
    {
        const auto section_it =
            std::find_if(m_sections.begin(), m_sections.end(), [&](const auto& s) { return s->name == section_name; });
        if (section_it == m_sections.end())
        {
            return {};
        }
        std::string text;
        for (const settings_field& field : (*section_it)->fields)
        {
            text += field.help_line;
        }
        return text;
    }

    void settings_registry::log_all_resolved() const
    {
        for (const auto& section : m_sections)
        {
            if (section->log_resolved)
            {
                section->log_resolved(section->instance);
            }
        }
    }
} // namespace core
