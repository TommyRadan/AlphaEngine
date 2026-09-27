// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file settings_registry.hpp
 * @brief Generic settings machinery: a registry of typed fields, grouped into module-owned sections, that
 *        @c core::load_settings resolves against compiled defaults, `settings.json`, the `ALPHAENGINE_*`
 *        environment variables and the command line — the same three layers every field used to have written
 *        out by hand.
 *
 * A module constructs its own settings struct (plain data, defined in that module), then registers one
 * @ref core::settings_section per struct and describes each field once: its JSON key, value kind, optional
 * range, optional environment variable name, optional command-line flag and a pre-formatted `--help` line.
 * The registry never sees the module's struct type — every field reaches it through small closures the
 * module supplies at registration, so this header names no renderer, window, audio or physics concept.
 * @c runtime (the composition root) calls every module's `register_settings` in a fixed order before
 * resolving; that order is what fixes the layout of `--help` and the "resolved" log lines.
 */

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace core
{
    /** @brief The lowercase word for @p value ("on" / "off"), for a resolved-settings log line. */
    const char* on_off(bool value) noexcept;

    /** @brief The scalar shape of one registered field, deciding which closures @ref settings_field uses. */
    enum class settings_value_kind
    {
        flag,   /**< A @c bool. */
        count,  /**< An @c unsigned @c int within an inclusive range. */
        number, /**< A @c float within an inclusive range. */
        text,   /**< A @c std::string, taken as-is from JSON and trimmed from the environment / command line. */
        choice, /**< A named alternative of a module-defined enum, resolved by name through a closure pair. */
    };

    /** @brief How a field's command-line flag (if it has one) takes its value. */
    enum class settings_cli_style
    {
        none,     /**< No generic `--flag <value>`; the field may still have @ref settings_field::choice_flags. */
        value,    /**< `--flag <value>` (or `--flag=value`). */
        presence, /**< `--flag` alone sets a @ref settings_value_kind::flag field true; it cannot be unset. */
    };

    /** @brief One exclusive command-line flag mapped to a named alternative of a @ref settings_value_kind::choice
     *         field (`--windowed`, `--fullscreen`, `--borderless` for a `window.mode`-shaped field), used instead
     *         of a generic `--flag <value>` option. */
    struct settings_choice_flag
    {
        std::string flag;       /**< e.g. "--windowed". */
        std::string value_name; /**< The name @ref settings_field::choice_set accepts for this flag. */
    };

    /**
     * @brief One named, typed value inside a @ref settings_section.
     *
     * Every accessor is a closure the owning module supplies at registration, bound to its own struct through
     * a captured pointer-to-member — the registry only ever calls them through the section's `void*` instance,
     * so it never names the module's type.
     */
    struct settings_field
    {
        std::string key; /**< The field's JSON key inside its section's object (e.g. "width"). */
        settings_value_kind kind{settings_value_kind::flag};

        // Exactly one of these is engaged, matching `kind`.
        std::function<bool&(void*)> flag_ref;
        std::function<unsigned int&(void*)> count_ref;
        std::function<float&(void*)> number_ref;
        std::function<std::string&(void*)> text_ref;

        // `choice` only: `choice_validate` is pure (no instance) so the command line can validate a value
        // before the file/environment layers have run; `choice_apply` assumes a validated name.
        std::function<bool(std::string_view)> choice_validate;
        std::function<void(void*, std::string_view)> choice_apply;
        std::function<std::string(const void*)> choice_get;
        /** @brief The phrase completing "settings: <key>='<value>' is not ...", e.g. "one of windowed|fullscreen|
         *         borderless" or "a supported graphics backend (vulkan)". `choice` only. */
        std::string choice_phrase;

        // `count` / `number` only: the inclusive range every layer clamps to.
        unsigned int count_min{0};
        unsigned int count_max{0};
        float number_min{0.0f};
        float number_max{0.0f};

        /** @brief e.g. "ALPHAENGINE_WIDTH"; unset means the field has no environment variable. */
        std::optional<std::string> env_name;

        /** @brief The generic `--flag` name (@ref settings_cli_style::value or ::presence), unset when the field
         *         is JSON/environment-only or reached only through @ref choice_flags. */
        std::optional<std::string> cli_flag;
        settings_cli_style cli_style{settings_cli_style::none};
        /** @brief Exclusive flags for a @c choice field (see @ref settings_choice_flag); empty for every other
         *         field and for a choice field reached through a generic `--flag <value>` instead. */
        std::vector<settings_choice_flag> choice_flags;

        /** @brief Pre-formatted, verbatim `--help` text for this field (its own trailing newline included), or
         *         empty when the field has no command-line surface. */
        std::string help_line;

        // -- Fluent configuration, chained onto the reference add_field / typed_section's add_* methods
        // return. Every field starts JSON-only; these add the optional environment and command-line surface.

        /** @brief Exposes this field as the environment variable @p name (e.g. "ALPHAENGINE_WIDTH"). */
        settings_field& with_env(std::string name)
        {
            env_name = std::move(name);
            return *this;
        }

        /** @brief Exposes this field as the command-line flag @p flag, taking a value unless @p style says
         *         otherwise (@ref settings_cli_style::presence for a flag field that cannot be unset). */
        settings_field& with_cli(std::string flag, settings_cli_style style = settings_cli_style::value)
        {
            cli_flag = std::move(flag);
            cli_style = style;
            return *this;
        }

        /** @brief Exposes a @c choice field through exclusive flags (see @ref settings_choice_flag) instead of
         *         a generic `--flag <value>` option. */
        settings_field& with_choice_flags(std::vector<settings_choice_flag> flags)
        {
            choice_flags = std::move(flags);
            return *this;
        }

        /** @brief Sets this field's pre-formatted `--help` text (see @ref help_line). */
        settings_field& with_help(std::string line)
        {
            help_line = std::move(line);
            return *this;
        }
    };

    /**
     * @brief A module-owned group of fields, all keyed under the same top-level `settings.json` object name.
     *
     * Built through @ref settings_registry::add_section; @p instance points at the module's own struct and is
     * handed back to every field's closures, so the registry stores it once per section rather than once per
     * field.
     */
    struct settings_section
    {
        explicit settings_section(std::string section_name, void* section_instance) noexcept
            : name(std::move(section_name)), instance(section_instance)
        {
        }

        std::string name;
        void* instance{nullptr};
        std::vector<settings_field> fields;

        /**
         * @brief Handles one JSON key this section recognises that no scalar @ref settings_field can express
         *        (only `input.bindings`' nested map needs this). Receives the section's own JSON object value
         *        for @p key; unset when every key is a plain scalar field.
         */
        std::function<void(void* instance, std::string_view key, const nlohmann::json& value)> custom_json;

        /** @brief Logs this section's resolved values at INFO, in whatever format the owning module chooses. */
        std::function<void(const void* instance)> log_resolved;

        settings_field& add_field(settings_field field);
    };

    /** @brief Environment lookup: the value of the named variable, or null when it is unset. */
    using environment_getter = std::function<const char*(const char* name)>;

    /** @brief What @ref settings_registry::parse_command_line recognised on a first pass over @c argv. */
    struct settings_command_line_result
    {
        bool help_requested{false};

        /** @brief The `--settings <path>` override; core-owned (picks which file @c load_settings reads), not
         *         backed by any registered field. */
        std::optional<std::string> settings_path;
        /** @brief The `--log-level <spec>` override; core-owned (@c core::logging), not backed by any registered
         *         field. */
        std::optional<std::string> log_level;

        // Every other recognised option's validated text, keyed by where it was found, applied later by
        // @ref settings_registry::apply_command_line so the command line stays the last-applied layer. Opaque
        // to callers outside core/settings*.
        struct resolved_value
        {
            std::size_t section_index{0};
            std::size_t field_index{0};
            std::string text;
        };
        std::vector<resolved_value> values;
    };

    /**
     * @brief The generic field/section registry every module's `register_settings` populates and
     *        @ref core::load_settings resolves against.
     */
    class settings_registry
    {
    public:
        /**
         * @brief Registers a new section named @p name, backed by @p instance (the module's own settings
         *        struct, which must outlive every call this registry makes into it — in practice, for the
         *        lifetime of the process). Sections are held so returned references stay valid regardless of
         *        how many further sections are added.
         */
        settings_section& add_section(std::string name, void* instance);

        /** @brief The registered sections, in registration order (for `--help` assembly by section name). */
        const std::vector<std::unique_ptr<settings_section>>& sections() const noexcept
        {
            return m_sections;
        }

        /**
         * @brief Applies a `settings.json` document on top of every registered section's instance. Only the
         *        keys present are applied; an unknown section, an unknown key, a value of the wrong JSON type
         *        or one outside its range is warned about (`LOG_WRN`) and skipped.
         * @return false when @p text is not a JSON object at all (nothing was applied), true otherwise.
         */
        bool apply_json(std::string_view text) const;

        /**
         * @brief Applies every registered field's environment variable (@ref settings_field::env_name) on top
         *        of its section's instance. An unset or blank variable leaves its field unchanged; an
         *        unparsable one is warned about and skipped.
         */
        void apply_environment(const environment_getter& get) const;

        /**
         * @brief Parses @p args (without the program name) into a @ref settings_command_line_result: `--help` /
         *        `-h` sets @c help_requested; `--settings` and `--log-level` are captured for the caller;
         *        every other recognised, valid `--flag` / `--flag=value` is recorded for @ref apply_command_line.
         *        An unknown option, a missing value or an unparsable value is warned about (`LOG_WRN`) and
         *        skipped, so parsing never fails; when an option repeats, the last valid occurrence wins.
         */
        settings_command_line_result parse_command_line(std::span<const char* const> args) const;

        /** @brief Applies every value @ref parse_command_line recorded on top of its section's instance. */
        void apply_command_line(const settings_command_line_result& result) const;

        /** @brief The `--help` lines of every field in the section named @p section_name, in registration
         *         order, concatenated verbatim; empty when no such section is registered. */
        std::string help_lines_for(std::string_view section_name) const;

        /** @brief Calls every registered section's @ref settings_section::log_resolved, in registration order;
         *         a section with none set is skipped. */
        void log_all_resolved() const;

    private:
        std::vector<std::unique_ptr<settings_section>> m_sections;
    };

    /**
     * @brief A type-safe view onto a @ref settings_section for one module struct @tparam S.
     *
     * Every `add_*` method takes a pointer-to-member of @c S, so the compiler — not a hand-written `void*`
     * cast at each registration site — guarantees the closure the registry stores actually reads and writes a
     * member of the struct @ref settings_section::instance points at. The handful of casts this implies are
     * written once, here, generically over @c S; a registration site (see e.g.
     * @c rendering_engine/settings_registration.cpp) never writes `static_cast<S*>` itself.
     */
    template<typename S>
    class typed_section
    {
    public:
        explicit typed_section(settings_section& section) noexcept : m_section(&section) {}

        /** @brief Registers a @c bool field bound to @p member. */
        settings_field& add_flag(std::string key, bool S::*member)
        {
            settings_field field{.key = std::move(key), .kind = settings_value_kind::flag};
            field.flag_ref = [member](void* p) -> bool& { return static_cast<S*>(p)->*member; };
            return m_section->add_field(std::move(field));
        }

        /** @brief Registers an @c unsigned @c int field bound to @p member, clamped to `[min, max]`. */
        settings_field& add_count(std::string key, unsigned int S::*member, unsigned int min, unsigned int max)
        {
            settings_field field{.key = std::move(key), .kind = settings_value_kind::count};
            field.count_ref = [member](void* p) -> unsigned int& { return static_cast<S*>(p)->*member; };
            field.count_min = min;
            field.count_max = max;
            return m_section->add_field(std::move(field));
        }

        /** @brief Registers a @c float field bound to @p member, clamped to `[min, max]`. */
        settings_field& add_number(std::string key, float S::*member, float min, float max)
        {
            settings_field field{.key = std::move(key), .kind = settings_value_kind::number};
            field.number_ref = [member](void* p) -> float& { return static_cast<S*>(p)->*member; };
            field.number_min = min;
            field.number_max = max;
            return m_section->add_field(std::move(field));
        }

        /** @brief Registers a @c std::string field bound to @p member, taken as-is (see @ref
         *         settings_value_kind::text). */
        settings_field& add_text(std::string key, std::string S::*member)
        {
            settings_field field{.key = std::move(key), .kind = settings_value_kind::text};
            field.text_ref = [member](void* p) -> std::string& { return static_cast<S*>(p)->*member; };
            return m_section->add_field(std::move(field));
        }

        /**
         * @brief Registers a named-alternative field (a module-defined enum) validated and applied through
         *        @p validate / @p apply / @p get, each already known to the module (typically thin wrappers
         *        around its own `parse_xxx` / `xxx_name` pair). Unlike the scalar `add_*` methods this takes
         *        no member pointer directly: @p apply and @p get receive the typed instance themselves, so a
         *        field whose value is not a plain assignment (or that validates against more than its own
         *        member) can still avoid a raw cast at the call site.
         * @param validate Pure: whether @p text names a valid alternative; no instance needed (so the command
         *                 line can validate before the file/environment layers have run).
         * @param apply    Assumes @p text already passed @p validate; assigns it into the instance.
         * @param get      The instance's current value, as the name @p validate / @p apply would accept.
         */
        template<typename Validate, typename Apply, typename Get>
        settings_field& add_choice(std::string key, Validate validate, Apply apply, Get get, std::string choice_phrase)
        {
            settings_field field{.key = std::move(key), .kind = settings_value_kind::choice};
            field.choice_validate = std::move(validate);
            field.choice_apply = [apply](void* p, std::string_view text) { apply(*static_cast<S*>(p), text); };
            field.choice_get = [get](const void* p) { return get(*static_cast<const S*>(p)); };
            field.choice_phrase = std::move(choice_phrase);
            return m_section->add_field(std::move(field));
        }

        /** @brief Sets the handler for a JSON key this section recognises that no scalar field can express
         *         (see @ref settings_section::custom_json); @p handler receives the typed instance. */
        template<typename Handler>
        void set_custom_json(Handler handler)
        {
            m_section->custom_json = [handler](void* p, std::string_view key, const nlohmann::json& value)
            { handler(*static_cast<S*>(p), key, value); };
        }

        /** @brief Sets this section's resolved-values logger (see @ref settings_section::log_resolved);
         *         @p logger receives the typed instance. */
        template<typename Logger>
        void set_log_resolved(Logger logger)
        {
            m_section->log_resolved = [logger](const void* p) { logger(*static_cast<const S*>(p)); };
        }

    private:
        settings_section* m_section;
    };

    /**
     * @brief Registers a new section named @p name, backed by @p instance, and returns a @ref typed_section
     *        wrapping it — the type-safe entry point every module's `register_settings` should use instead of
     *        calling @ref settings_registry::add_section directly.
     */
    template<typename S>
    typed_section<S> add_typed_section(settings_registry& registry, std::string name, S& instance)
    {
        return typed_section<S>(registry.add_section(std::move(name), &instance));
    }
} // namespace core
