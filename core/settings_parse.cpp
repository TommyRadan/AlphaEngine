// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/settings_parse.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

#include <core/log.hpp>

namespace core
{
    namespace
    {
        using json = nlohmann::json;

        std::string_view trim(std::string_view text)
        {
            const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!text.empty() && is_space(text.front()))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && is_space(text.back()))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        bool equals_ignoring_case(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                {
                    return false;
                }
            }
            return true;
        }

        template<typename T>
        void assign_if(T& target, const std::optional<T>& value)
        {
            if (value.has_value())
            {
                target = *value;
            }
        }

        // Keeps a previously parsed value when a later occurrence fails to parse.
        template<typename T>
        void store_if(std::optional<T>& target, const std::optional<T>& value)
        {
            if (value.has_value())
            {
                target = value;
            }
        }

        // -- Text parsers with the warning attached ---------------------------
        //
        // Shared by the environment and command-line layers; `source` names
        // the variable or option in the message.

        std::optional<unsigned int>
        parse_unsigned_or_warn(const char* source, std::string_view text, unsigned int min, unsigned int max)
        {
            const auto parsed = parse_unsigned(text, min, max);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not an integer in [%u, %u]; ignoring it",
                        source,
                        std::string{text}.c_str(),
                        min,
                        max);
            }
            return parsed;
        }

        std::optional<float> parse_float_or_warn(const char* source, std::string_view text, float min, float max)
        {
            const auto parsed = parse_float(text, min, max);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not a number in [%g, %g]; ignoring it",
                        source,
                        std::string{text}.c_str(),
                        static_cast<double>(min),
                        static_cast<double>(max));
            }
            return parsed;
        }

        std::optional<bool> parse_bool_or_warn(const char* source, std::string_view text)
        {
            const auto parsed = parse_bool(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not a boolean (true/false, 1/0, yes/no, on/off); ignoring it",
                        source,
                        std::string{text}.c_str());
            }
            return parsed;
        }

        std::optional<window_mode> parse_window_mode_or_warn(const char* source, std::string_view text)
        {
            const auto parsed = parse_window_mode(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not one of windowed|fullscreen|borderless; ignoring it",
                        source,
                        std::string{text}.c_str());
            }
            return parsed;
        }

        std::optional<graphics_backend> parse_graphics_backend_or_warn(const char* source, std::string_view text)
        {
            const auto parsed = parse_graphics_backend(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not a supported graphics backend (vulkan); ignoring it",
                        source,
                        std::string{text}.c_str());
            }
            return parsed;
        }

        std::optional<tonemap_curve> parse_tonemap_curve_or_warn(const char* source, std::string_view text)
        {
            const auto parsed = parse_tonemap_curve(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not one of none|reinhard|aces; ignoring it",
                        source,
                        std::string{text}.c_str());
            }
            return parsed;
        }

        // -- JSON -------------------------------------------------------------
        //
        // One setter per field type. Each checks the JSON type and the range
        // and warns with the dotted key on a mismatch, leaving the target as
        // it was.

        void set_from_json(unsigned int& target, const char* key, const json& value, unsigned int min, unsigned int max)
        {
            if (!value.is_number_unsigned())
            {
                LOG_WRN("settings: %s must be an unsigned integer; keeping %u", key, target);
                return;
            }
            const auto raw = value.get<std::uint64_t>();
            if (raw < min || raw > max)
            {
                LOG_WRN("settings: %s=%llu is outside [%u, %u]; keeping %u",
                        key,
                        static_cast<unsigned long long>(raw),
                        min,
                        max,
                        target);
                return;
            }
            target = static_cast<unsigned int>(raw);
        }

        void set_from_json(float& target, const char* key, const json& value, float min, float max)
        {
            if (!value.is_number())
            {
                LOG_WRN("settings: %s must be a number; keeping %g", key, static_cast<double>(target));
                return;
            }
            const double raw = value.get<double>();
            if (!std::isfinite(raw) || raw < static_cast<double>(min) || raw > static_cast<double>(max))
            {
                LOG_WRN("settings: %s=%g is outside [%g, %g]; keeping %g",
                        key,
                        raw,
                        static_cast<double>(min),
                        static_cast<double>(max),
                        static_cast<double>(target));
                return;
            }
            target = static_cast<float>(raw);
        }

        void set_from_json(bool& target, const char* key, const json& value)
        {
            if (!value.is_boolean())
            {
                LOG_WRN("settings: %s must be true or false; keeping %s", key, target ? "true" : "false");
                return;
            }
            target = value.get<bool>();
        }

        void set_from_json(std::string& target, const char* key, const json& value)
        {
            if (!value.is_string())
            {
                LOG_WRN("settings: %s must be a string; keeping '%s'", key, target.c_str());
                return;
            }
            target = value.get<std::string>();
        }

        void set_from_json(window_mode& target, const char* key, const json& value)
        {
            if (!value.is_string())
            {
                LOG_WRN("settings: %s must be a string; keeping %s", key, window_mode_name(target));
                return;
            }
            const auto& text = value.get_ref<const std::string&>();
            const auto parsed = parse_window_mode(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not one of windowed|fullscreen|borderless; keeping %s",
                        key,
                        text.c_str(),
                        window_mode_name(target));
                return;
            }
            target = *parsed;
        }

        void set_from_json(graphics_backend& target, const char* key, const json& value)
        {
            if (!value.is_string())
            {
                LOG_WRN("settings: %s must be a string; keeping %s", key, graphics_backend_name(target));
                return;
            }
            const auto& text = value.get_ref<const std::string&>();
            const auto parsed = parse_graphics_backend(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not a supported graphics backend (vulkan); keeping %s",
                        key,
                        text.c_str(),
                        graphics_backend_name(target));
                return;
            }
            target = *parsed;
        }

        void set_from_json(tonemap_curve& target, const char* key, const json& value)
        {
            if (!value.is_string())
            {
                LOG_WRN("settings: %s must be a string; keeping %s", key, tonemap_curve_name(target));
                return;
            }
            const auto& text = value.get_ref<const std::string&>();
            const auto parsed = parse_tonemap_curve(text);
            if (!parsed.has_value())
            {
                LOG_WRN("settings: %s='%s' is not one of none|reinhard|aces; keeping %s",
                        key,
                        text.c_str(),
                        tonemap_curve_name(target));
                return;
            }
            target = *parsed;
        }

        // input.bindings: { "<action or axis name>": ["<binding string>", ...], ... }. The binding-string
        // grammar itself is core::input's concern (core/input.cpp); this only captures the raw strings, tolerant
        // the same way as everything else here, so a bad entry is warned about and skipped rather than dropping
        // the whole section.
        void set_bindings_from_json(std::unordered_map<std::string, std::vector<std::string>>& target,
                                    const json& value)
        {
            if (!value.is_object())
            {
                LOG_WRN("settings: input.bindings must be a JSON object; ignoring it");
                return;
            }
            for (const auto& [name, list] : value.items())
            {
                if (!list.is_array())
                {
                    LOG_WRN("settings: input.bindings.%s must be an array of strings; ignoring it", name.c_str());
                    continue;
                }
                std::vector<std::string> bindings;
                for (const auto& entry : list)
                {
                    if (!entry.is_string())
                    {
                        LOG_WRN("settings: input.bindings.%s has a non-string entry; skipping it", name.c_str());
                        continue;
                    }
                    bindings.push_back(entry.get<std::string>());
                }
                target[name] = std::move(bindings);
            }
        }

        void warn_unknown_key(const char* section, const std::string& key)
        {
            LOG_WRN("settings: ignoring unknown key '%s.%s'", section, key.c_str());
        }

        void apply_window_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "width")
                {
                    set_from_json(out.window.width, "window.width", value, 0, k_max_window_dimension);
                }
                else if (key == "height")
                {
                    set_from_json(out.window.height, "window.height", value, 0, k_max_window_dimension);
                }
                else if (key == "mode")
                {
                    set_from_json(out.window.mode, "window.mode", value);
                }
                else if (key == "vsync")
                {
                    set_from_json(out.window.vsync, "window.vsync", value);
                }
                else if (key == "title")
                {
                    set_from_json(out.window.title, "window.title", value);
                }
                else
                {
                    warn_unknown_key("window", key);
                }
            }
        }

        void apply_graphics_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "backend")
                {
                    set_from_json(out.graphics.backend, "graphics.backend", value);
                }
                else if (key == "temporal_aa")
                {
                    set_from_json(out.graphics.temporal_aa, "graphics.temporal_aa", value);
                }
                else if (key == "depth_prepass")
                {
                    set_from_json(out.graphics.depth_prepass, "graphics.depth_prepass", value);
                }
                else if (key == "frames_in_flight")
                {
                    set_from_json(out.graphics.frames_in_flight,
                                  "graphics.frames_in_flight",
                                  value,
                                  1,
                                  graphics_settings::max_frames_in_flight);
                }
                else if (key == "parallel_draw_threshold")
                {
                    set_from_json(out.graphics.parallel_draw_threshold,
                                  "graphics.parallel_draw_threshold",
                                  value,
                                  0,
                                  k_max_parallel_draw_threshold);
                }
                else
                {
                    warn_unknown_key("graphics", key);
                }
            }
        }

        void apply_content_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "root")
                {
                    set_from_json(out.content.root, "content.root", value);
                }
                else
                {
                    warn_unknown_key("content", key);
                }
            }
        }

        void apply_diagnostics_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "frame_limit")
                {
                    set_from_json(out.diagnostics.frame_limit, "diagnostics.frame_limit", value, 0, k_max_frame_limit);
                }
                else if (key == "fail_on_error")
                {
                    set_from_json(out.diagnostics.fail_on_error, "diagnostics.fail_on_error", value);
                }
                else
                {
                    warn_unknown_key("diagnostics", key);
                }
            }
        }

        void apply_camera_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "fov_degrees")
                {
                    set_from_json(out.camera.field_of_view,
                                  "camera.fov_degrees",
                                  value,
                                  k_min_field_of_view,
                                  k_max_field_of_view);
                }
                else
                {
                    warn_unknown_key("camera", key);
                }
            }
        }

        void apply_input_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "mouse_sensitivity")
                {
                    set_from_json(out.input.mouse_sensitivity,
                                  "input.mouse_sensitivity",
                                  value,
                                  k_min_mouse_sensitivity,
                                  k_max_mouse_sensitivity);
                }
                else if (key == "mouse_reversed")
                {
                    set_from_json(out.input.mouse_reversed, "input.mouse_reversed", value);
                }
                else if (key == "bindings")
                {
                    set_bindings_from_json(out.input.bindings, value);
                }
                else
                {
                    warn_unknown_key("input", key);
                }
            }
        }

        void apply_shadows_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (key == "resolution")
                {
                    set_from_json(out.shadows.resolution,
                                  "shadows.resolution",
                                  value,
                                  k_min_shadow_resolution,
                                  k_max_shadow_resolution);
                }
                else if (key == "distance")
                {
                    set_from_json(
                        out.shadows.distance, "shadows.distance", value, k_min_shadow_distance, k_max_shadow_distance);
                }
                else if (key == "cascade_count")
                {
                    set_from_json(out.shadows.cascade_count,
                                  "shadows.cascade_count",
                                  value,
                                  1,
                                  shadow_settings::max_cascade_count);
                }
                else if (key == "bias")
                {
                    set_from_json(out.shadows.bias, "shadows.bias", value, 0.0f, k_max_shadow_bias);
                }
                else if (key == "slope_bias")
                {
                    set_from_json(out.shadows.slope_bias, "shadows.slope_bias", value, 0.0f, k_max_shadow_slope_bias);
                }
                else if (key == "pcf_kernel")
                {
                    set_from_json(
                        out.shadows.pcf_kernel, "shadows.pcf_kernel", value, 1, shadow_settings::max_pcf_kernel);
                }
                else
                {
                    warn_unknown_key("shadows", key);
                }
            }
        }

        // -- Post-processing --------------------------------------------------
        //
        // post_process_settings is flat, and each field is reachable from all
        // three layers under one name: its key in the settings.json `post`
        // section, the ALPHAENGINE_<KEY> environment variable and the --<key>
        // option (dashes for underscores). One table row per field keeps the
        // three layers in step; the value parsing, ranges and warnings are the
        // same helpers every other section uses.

        enum class post_value_kind
        {
            flag,
            number,
            count,
            curve,
            path,
        };

        struct post_field
        {
            std::string_view key;
            post_value_kind kind;
            bool post_process_settings::*flag{nullptr};
            float post_process_settings::*number{nullptr};
            unsigned int post_process_settings::*count{nullptr};
            tonemap_curve post_process_settings::*curve{nullptr};
            std::string post_process_settings::*path{nullptr};
            float number_min{0.0f};
            float number_max{0.0f};
            unsigned int count_min{0};
            unsigned int count_max{0};
        };

        constexpr post_field flag_field(std::string_view key, bool post_process_settings::*member)
        {
            post_field field{key, post_value_kind::flag};
            field.flag = member;
            return field;
        }

        constexpr post_field
        number_field(std::string_view key, float post_process_settings::*member, float min, float max)
        {
            post_field field{key, post_value_kind::number};
            field.number = member;
            field.number_min = min;
            field.number_max = max;
            return field;
        }

        constexpr post_field count_field(std::string_view key,
                                         unsigned int post_process_settings::*member,
                                         unsigned int min,
                                         unsigned int max)
        {
            post_field field{key, post_value_kind::count};
            field.count = member;
            field.count_min = min;
            field.count_max = max;
            return field;
        }

        constexpr post_field curve_field(std::string_view key, tonemap_curve post_process_settings::*member)
        {
            post_field field{key, post_value_kind::curve};
            field.curve = member;
            return field;
        }

        constexpr post_field path_field(std::string_view key, std::string post_process_settings::*member)
        {
            post_field field{key, post_value_kind::path};
            field.path = member;
            return field;
        }

        using post_values = post_process_settings;

        constexpr post_field k_post_fields[] = {
            number_field("exposure", &post_values::exposure, 0.0f, k_max_exposure),
            curve_field("tonemap", &post_values::tonemap),
            flag_field("bloom", &post_values::bloom),
            number_field("bloom_threshold", &post_values::bloom_threshold, 0.0f, k_max_bloom_threshold),
            number_field("bloom_knee", &post_values::bloom_knee, 0.0f, 1.0f),
            number_field("bloom_strength", &post_values::bloom_strength, 0.0f, k_max_bloom_strength),
            number_field("taa_feedback", &post_values::taa_feedback, 0.0f, k_max_taa_feedback),
            flag_field("fxaa", &post_values::fxaa),
            flag_field("volumetric_fog", &post_values::volumetric_fog),
            number_field("volumetric_fog_density_scale",
                         &post_values::volumetric_fog_density_scale,
                         0.0f,
                         k_max_volumetric_fog_density_scale),
            number_field("volumetric_fog_anisotropy",
                         &post_values::volumetric_fog_anisotropy,
                         -k_max_volumetric_fog_anisotropy,
                         k_max_volumetric_fog_anisotropy),
            number_field("volumetric_fog_max_distance",
                         &post_values::volumetric_fog_max_distance,
                         0.0f,
                         k_max_volumetric_fog_distance),
            count_field("volumetric_fog_steps", &post_values::volumetric_fog_steps, 1, k_max_volumetric_fog_steps),
            number_field("volumetric_fog_intensity",
                         &post_values::volumetric_fog_intensity,
                         0.0f,
                         k_max_volumetric_fog_intensity),
            path_field("grading_lut", &post_values::grading_lut),
            number_field("grading_intensity", &post_values::grading_intensity, 0.0f, 1.0f),
            flag_field("motion_blur", &post_values::motion_blur),
            number_field(
                "motion_blur_intensity", &post_values::motion_blur_intensity, 0.0f, k_max_motion_blur_intensity),
            count_field("motion_blur_samples",
                        &post_values::motion_blur_samples,
                        k_min_motion_blur_samples,
                        k_max_motion_blur_samples),
            number_field("motion_blur_max_radius",
                         &post_values::motion_blur_max_radius,
                         k_min_motion_blur_radius,
                         k_max_motion_blur_radius),
            flag_field("auto_exposure", &post_values::auto_exposure),
            number_field(
                "auto_exposure_min_ev", &post_values::auto_exposure_min_ev, k_min_exposure_ev, k_max_exposure_ev),
            number_field(
                "auto_exposure_max_ev", &post_values::auto_exposure_max_ev, k_min_exposure_ev, k_max_exposure_ev),
            number_field("auto_exposure_speed_up", &post_values::auto_exposure_speed_up, 0.0f, k_max_exposure_speed),
            number_field(
                "auto_exposure_speed_down", &post_values::auto_exposure_speed_down, 0.0f, k_max_exposure_speed),
            number_field("auto_exposure_compensation",
                         &post_values::auto_exposure_compensation,
                         -k_max_exposure_compensation,
                         k_max_exposure_compensation),
        };

        // ALPHAENGINE_<KEY>: the field's environment variable.
        std::string post_variable_name(const post_field& field)
        {
            std::string name{"ALPHAENGINE_"};
            for (const char c : field.key)
            {
                name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            }
            return name;
        }

        // --<key> with dashes for underscores: the field's command-line option.
        std::string post_option_name(const post_field& field)
        {
            std::string name{"--"};
            for (const char c : field.key)
            {
                name.push_back(c == '_' ? '-' : c);
            }
            return name;
        }

        const post_field* find_post_field_by_key(std::string_view key)
        {
            for (const auto& field : k_post_fields)
            {
                if (field.key == key)
                {
                    return &field;
                }
            }
            return nullptr;
        }

        const post_field* find_post_field_by_option(std::string_view option)
        {
            for (const auto& field : k_post_fields)
            {
                if (post_option_name(field) == option)
                {
                    return &field;
                }
            }
            return nullptr;
        }

        void set_post_from_json(post_process_settings& out, const post_field& field, const json& value)
        {
            const std::string key = "post." + std::string{field.key};
            switch (field.kind)
            {
            case post_value_kind::flag:
                set_from_json(out.*field.flag, key.c_str(), value);
                break;
            case post_value_kind::number:
                set_from_json(out.*field.number, key.c_str(), value, field.number_min, field.number_max);
                break;
            case post_value_kind::count:
                set_from_json(out.*field.count, key.c_str(), value, field.count_min, field.count_max);
                break;
            case post_value_kind::curve:
                set_from_json(out.*field.curve, key.c_str(), value);
                break;
            case post_value_kind::path:
                set_from_json(out.*field.path, key.c_str(), value);
                break;
            }
        }

        // Parses @p text as @p field's value, warning under @p source (the
        // variable or option) when it does not parse, and stores it. Returns
        // whether it parsed. A path is taken as-is, trimmed.
        bool set_post_from_text(post_process_settings& out,
                                const post_field& field,
                                std::string_view text,
                                const char* source)
        {
            switch (field.kind)
            {
            case post_value_kind::flag:
            {
                const auto parsed = parse_bool_or_warn(source, text);
                assign_if(out.*field.flag, parsed);
                return parsed.has_value();
            }
            case post_value_kind::number:
            {
                const auto parsed = parse_float_or_warn(source, text, field.number_min, field.number_max);
                assign_if(out.*field.number, parsed);
                return parsed.has_value();
            }
            case post_value_kind::count:
            {
                const auto parsed = parse_unsigned_or_warn(source, text, field.count_min, field.count_max);
                assign_if(out.*field.count, parsed);
                return parsed.has_value();
            }
            case post_value_kind::curve:
            {
                const auto parsed = parse_tonemap_curve_or_warn(source, text);
                assign_if(out.*field.curve, parsed);
                return parsed.has_value();
            }
            case post_value_kind::path:
                out.*field.path = std::string{trim(text)};
                return true;
            }
            return false;
        }

        void apply_post_section(settings& out, const json& section)
        {
            for (const auto& [key, value] : section.items())
            {
                if (const post_field* field = find_post_field_by_key(key))
                {
                    set_post_from_json(out.post, *field, value);
                }
                else
                {
                    warn_unknown_key("post", key);
                }
            }
        }

        // -- Command line -----------------------------------------------------

        enum class value_option
        {
            width,
            height,
            backend,
            vsync,
            frames_in_flight,
            parallel_draw_threshold,
            shadow_resolution,
            shadow_distance,
            shadow_cascades,
            shadow_bias,
            shadow_slope_bias,
            shadow_pcf_kernel,
            log_level,
            settings_path,
            content_root,
            frame_limit
        };

        struct value_option_entry
        {
            std::string_view name;
            value_option kind;
        };

        constexpr value_option_entry k_value_options[] = {
            {"--width", value_option::width},
            {"--height", value_option::height},
            {"--backend", value_option::backend},
            {"--vsync", value_option::vsync},
            {"--frames-in-flight", value_option::frames_in_flight},
            {"--parallel-draw-threshold", value_option::parallel_draw_threshold},
            {"--shadow-resolution", value_option::shadow_resolution},
            {"--shadow-distance", value_option::shadow_distance},
            {"--shadow-cascades", value_option::shadow_cascades},
            {"--shadow-bias", value_option::shadow_bias},
            {"--shadow-slope-bias", value_option::shadow_slope_bias},
            {"--shadow-pcf-kernel", value_option::shadow_pcf_kernel},
            {"--log-level", value_option::log_level},
            {"--settings", value_option::settings_path},
            {"--content-root", value_option::content_root},
            {"--frames", value_option::frame_limit},
        };

        struct mode_flag_entry
        {
            std::string_view name;
            window_mode mode;
        };

        constexpr mode_flag_entry k_mode_flags[] = {
            {"--windowed", window_mode::windowed},
            {"--fullscreen", window_mode::fullscreen},
            {"--borderless", window_mode::borderless},
        };

        // Presence-only boolean flags: like the mode flags above, these take no value and are simply on when
        // given, so a repeat is harmless and there is no way to turn one back off from the command line.
        struct bool_flag_entry
        {
            std::string_view name;
            bool command_line_options::*field;
        };

        constexpr bool_flag_entry k_bool_flags[] = {
            {"--fail-on-error", &command_line_options::fail_on_error},
        };

        std::optional<value_option> find_value_option(std::string_view name)
        {
            for (const auto& entry : k_value_options)
            {
                if (entry.name == name)
                {
                    return entry.kind;
                }
            }
            return std::nullopt;
        }

        std::optional<window_mode> find_mode_flag(std::string_view name)
        {
            for (const auto& entry : k_mode_flags)
            {
                if (entry.name == name)
                {
                    return entry.mode;
                }
            }
            return std::nullopt;
        }

        const bool_flag_entry* find_bool_flag(std::string_view name)
        {
            for (const auto& entry : k_bool_flags)
            {
                if (entry.name == name)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        bool looks_like_option(const char* arg)
        {
            return arg != nullptr && std::string_view{arg}.starts_with("--");
        }

        constexpr const char k_usage[] = R"(Usage: AlphaEngine [options]

Options:
  --width <n>              window width in points (0 = match the display)
  --height <n>             window height in points (0 = match the display)
  --windowed               decorated window
  --fullscreen             fullscreen at the display's resolution
  --borderless             borderless window
  --backend <name>         gpu backend: vulkan
  --vsync <on|off>         wait for vertical sync
  --frames-in-flight <n>   frames the vulkan backend keeps in flight, 1 to 2
  --parallel-draw-threshold <n>
                           draws above which the scene pass records in parallel
                           on vulkan (and per recording chunk); 0 disables
  --shadow-resolution <n>  texels per side of the shadow maps
  --shadow-distance <d>    view depth the directional cascades cover
  --shadow-cascades <n>    directional shadow cascades, 1 to 4
  --shadow-bias <b>        directional receiver depth bias
  --shadow-slope-bias <s>  rasterizer slope bias of the shadow passes
  --shadow-pcf-kernel <n>  hardware pcf taps per side, 1 to 8
  --log-level <spec>       log level, e.g. warn or info,gpu=trace
  --settings <path>        settings file to read instead of the default
  --content-root <path>    directory relative asset paths resolve under
  --frames <n>             quit after rendering n frames (0 = run forever, the default)
  --fail-on-error          exit non-zero if any [ERR] line is logged, not only [FTL]
  -h, --help               print this text and exit

Post-processing options (settings.json "post" section; each key is also the
ALPHAENGINE_<KEY> variable, e.g. --bloom-threshold, post.bloom_threshold and
ALPHAENGINE_BLOOM_THRESHOLD):
  --exposure <e>                        manual exposure scale
  --tonemap <curve>                     tonemap curve: none, reinhard or aces
  --bloom <on|off>                      hdr bloom glow
  --bloom-threshold <t>                 luminance above which pixels bloom
  --bloom-knee <k>                      bloom soft knee, 0 to 1
  --bloom-strength <s>                  bloom glow strength
  --taa-feedback <f>                    temporal-aa history weight, 0 to 0.99
  --fxaa <on|off>                       fast approximate anti-aliasing
  --volumetric-fog <on|off>             raymarched height fog
  --volumetric-fog-density-scale <d>    volumetric fog density multiplier
  --volumetric-fog-anisotropy <g>       volumetric phase anisotropy, -0.99 to 0.99
  --volumetric-fog-max-distance <d>     distance the volumetric march stops at
  --volumetric-fog-steps <n>            volumetric march steps, 1 to 128
  --volumetric-fog-intensity <i>        volumetric in-scattering scale
  --grading-lut <path>                  colour-grading strip lut (n*n x n)
  --grading-intensity <i>               colour-grading blend, 0 to 1
  --motion-blur <on|off>                motion blur from the velocity buffer
  --motion-blur-intensity <s>           shutter scale, 0 to 2
  --motion-blur-samples <n>             taps per pixel, 2 to 32
  --motion-blur-max-radius <px>         longest blur in pixels, 1 to 256
  --auto-exposure <on|off>              eye adaptation instead of --exposure
  --auto-exposure-min-ev <ev>           lowest metered brightness, in ev100
  --auto-exposure-max-ev <ev>           highest metered brightness, in ev100
  --auto-exposure-speed-up <r>          adaptation rate toward brighter
  --auto-exposure-speed-down <r>        adaptation rate toward darker
  --auto-exposure-compensation <stops>  stops added to the adapted exposure

Every option also accepts the --key=value form. Command-line values override
the ALPHAENGINE_* environment variables, which override the settings file.
)";
    } // namespace

    std::optional<bool> parse_bool(std::string_view text)
    {
        text = trim(text);
        constexpr std::string_view k_true_words[] = {"true", "1", "yes", "on"};
        constexpr std::string_view k_false_words[] = {"false", "0", "no", "off"};
        for (const std::string_view word : k_true_words)
        {
            if (equals_ignoring_case(text, word))
            {
                return true;
            }
        }
        for (const std::string_view word : k_false_words)
        {
            if (equals_ignoring_case(text, word))
            {
                return false;
            }
        }
        return std::nullopt;
    }

    std::optional<unsigned int> parse_unsigned(std::string_view text, unsigned int min, unsigned int max)
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        unsigned int value = 0;
        const char* const end = text.data() + text.size();
        // from_chars rejects a leading sign for unsigned targets, and the
        // pointer check rejects trailing characters.
        const auto [ptr, ec] = std::from_chars(text.data(), end, value, 10);
        if (ec != std::errc{} || ptr != end)
        {
            return std::nullopt;
        }
        if (value < min || value > max)
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<float> parse_float(std::string_view text, float min, float max)
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        // A classic-locale stream rather than strtof so a decimal-comma
        // process locale cannot change what "0.5" means, and rather than
        // from_chars because not every supported standard library ships the
        // floating-point overloads yet.
        std::istringstream stream{std::string{text}};
        stream.imbue(std::locale::classic());
        double value = 0.0;
        stream >> value;
        if (stream.fail() || stream.peek() != std::char_traits<char>::eof())
        {
            return std::nullopt;
        }
        if (!std::isfinite(value) || value < static_cast<double>(min) || value > static_cast<double>(max))
        {
            return std::nullopt;
        }
        return static_cast<float>(value);
    }

    std::optional<window_mode> parse_window_mode(std::string_view text)
    {
        text = trim(text);
        constexpr window_mode k_all_modes[] = {window_mode::windowed, window_mode::fullscreen, window_mode::borderless};
        for (const window_mode mode : k_all_modes)
        {
            if (equals_ignoring_case(text, window_mode_name(mode)))
            {
                return mode;
            }
        }
        return std::nullopt;
    }

    std::optional<graphics_backend> parse_graphics_backend(std::string_view text)
    {
        text = trim(text);
        constexpr graphics_backend k_all_backends[] = {graphics_backend::vulkan};
        for (const graphics_backend backend : k_all_backends)
        {
            if (equals_ignoring_case(text, graphics_backend_name(backend)))
            {
                return backend;
            }
        }
        return std::nullopt;
    }

    std::optional<tonemap_curve> parse_tonemap_curve(std::string_view text)
    {
        text = trim(text);
        constexpr tonemap_curve k_all_curves[] = {tonemap_curve::none, tonemap_curve::reinhard, tonemap_curve::aces};
        for (const tonemap_curve curve : k_all_curves)
        {
            if (equals_ignoring_case(text, tonemap_curve_name(curve)))
            {
                return curve;
            }
        }
        return std::nullopt;
    }

    bool apply_json(settings& out, std::string_view text)
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

        for (const auto& [name, section] : document.items())
        {
            if (!section.is_object())
            {
                LOG_WRN("settings: '%s' must be a JSON object; ignoring it", name.c_str());
                continue;
            }
            if (name == "window")
            {
                apply_window_section(out, section);
            }
            else if (name == "graphics")
            {
                apply_graphics_section(out, section);
            }
            else if (name == "camera")
            {
                apply_camera_section(out, section);
            }
            else if (name == "input")
            {
                apply_input_section(out, section);
            }
            else if (name == "shadows")
            {
                apply_shadows_section(out, section);
            }
            else if (name == "post")
            {
                apply_post_section(out, section);
            }
            else if (name == "content")
            {
                apply_content_section(out, section);
            }
            else if (name == "diagnostics")
            {
                apply_diagnostics_section(out, section);
            }
            else
            {
                LOG_WRN("settings: ignoring unknown section '%s'", name.c_str());
            }
        }
        return true;
    }

    void apply_environment(settings& out, const environment_getter& get)
    {
        // An unset or blank variable is "not configured", never a warning.
        const auto read = [&get](const char* name) -> std::optional<std::string_view>
        {
            const char* value = get ? get(name) : nullptr;
            if (value == nullptr || trim(value).empty())
            {
                return std::nullopt;
            }
            return std::string_view{value};
        };

        if (const auto text = read("ALPHAENGINE_WIDTH"))
        {
            assign_if(out.window.width, parse_unsigned_or_warn("ALPHAENGINE_WIDTH", *text, 0, k_max_window_dimension));
        }
        if (const auto text = read("ALPHAENGINE_HEIGHT"))
        {
            assign_if(out.window.height,
                      parse_unsigned_or_warn("ALPHAENGINE_HEIGHT", *text, 0, k_max_window_dimension));
        }
        if (const auto text = read("ALPHAENGINE_WINDOW_MODE"))
        {
            assign_if(out.window.mode, parse_window_mode_or_warn("ALPHAENGINE_WINDOW_MODE", *text));
        }
        if (const auto text = read("ALPHAENGINE_VSYNC"))
        {
            assign_if(out.window.vsync, parse_bool_or_warn("ALPHAENGINE_VSYNC", *text));
        }
        if (const auto text = read("ALPHAENGINE_GRAPHICS_BACKEND"))
        {
            assign_if(out.graphics.backend, parse_graphics_backend_or_warn("ALPHAENGINE_GRAPHICS_BACKEND", *text));
        }
        if (const auto text = read("ALPHAENGINE_TAA"))
        {
            assign_if(out.graphics.temporal_aa, parse_bool_or_warn("ALPHAENGINE_TAA", *text));
        }
        if (const auto text = read("ALPHAENGINE_DEPTH_PREPASS"))
        {
            assign_if(out.graphics.depth_prepass, parse_bool_or_warn("ALPHAENGINE_DEPTH_PREPASS", *text));
        }
        if (const auto text = read("ALPHAENGINE_FRAMES_IN_FLIGHT"))
        {
            assign_if(out.graphics.frames_in_flight,
                      parse_unsigned_or_warn(
                          "ALPHAENGINE_FRAMES_IN_FLIGHT", *text, 1, graphics_settings::max_frames_in_flight));
        }
        if (const auto text = read("ALPHAENGINE_PARALLEL_DRAW_THRESHOLD"))
        {
            assign_if(
                out.graphics.parallel_draw_threshold,
                parse_unsigned_or_warn("ALPHAENGINE_PARALLEL_DRAW_THRESHOLD", *text, 0, k_max_parallel_draw_threshold));
        }
        if (const auto text = read("ALPHAENGINE_SHADOW_RESOLUTION"))
        {
            assign_if(out.shadows.resolution,
                      parse_unsigned_or_warn(
                          "ALPHAENGINE_SHADOW_RESOLUTION", *text, k_min_shadow_resolution, k_max_shadow_resolution));
        }
        if (const auto text = read("ALPHAENGINE_SHADOW_DISTANCE"))
        {
            assign_if(out.shadows.distance,
                      parse_float_or_warn(
                          "ALPHAENGINE_SHADOW_DISTANCE", *text, k_min_shadow_distance, k_max_shadow_distance));
        }
        if (const auto text = read("ALPHAENGINE_SHADOW_CASCADES"))
        {
            assign_if(
                out.shadows.cascade_count,
                parse_unsigned_or_warn("ALPHAENGINE_SHADOW_CASCADES", *text, 1, shadow_settings::max_cascade_count));
        }
        if (const auto text = read("ALPHAENGINE_SHADOW_BIAS"))
        {
            assign_if(out.shadows.bias, parse_float_or_warn("ALPHAENGINE_SHADOW_BIAS", *text, 0.0f, k_max_shadow_bias));
        }
        if (const auto text = read("ALPHAENGINE_SHADOW_SLOPE_BIAS"))
        {
            assign_if(out.shadows.slope_bias,
                      parse_float_or_warn("ALPHAENGINE_SHADOW_SLOPE_BIAS", *text, 0.0f, k_max_shadow_slope_bias));
        }
        if (const auto text = read("ALPHAENGINE_SHADOW_PCF_KERNEL"))
        {
            assign_if(
                out.shadows.pcf_kernel,
                parse_unsigned_or_warn("ALPHAENGINE_SHADOW_PCF_KERNEL", *text, 1, shadow_settings::max_pcf_kernel));
        }
        for (const auto& field : k_post_fields)
        {
            const std::string variable = post_variable_name(field);
            if (const auto text = read(variable.c_str()))
            {
                set_post_from_text(out.post, field, *text, variable.c_str());
            }
        }
        if (const auto text = read("ALPHAENGINE_CONTENT_ROOT"))
        {
            out.content.root = std::string{trim(*text)};
        }
        if (const auto text = read("ALPHAENGINE_FRAMES"))
        {
            assign_if(out.diagnostics.frame_limit,
                      parse_unsigned_or_warn("ALPHAENGINE_FRAMES", *text, 0, k_max_frame_limit));
        }
        if (const auto text = read("ALPHAENGINE_FAIL_ON_ERROR"))
        {
            assign_if(out.diagnostics.fail_on_error, parse_bool_or_warn("ALPHAENGINE_FAIL_ON_ERROR", *text));
        }
    }

    command_line_options parse_command_line(std::span<const char* const> args)
    {
        command_line_options out;
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

            if (const auto mode = find_mode_flag(name); mode.has_value())
            {
                if (value.has_value())
                {
                    LOG_WRN("settings: %s takes no value; ignoring it", name.c_str());
                    continue;
                }
                out.mode = *mode;
                continue;
            }

            if (const bool_flag_entry* flag = find_bool_flag(name); flag != nullptr)
            {
                if (value.has_value())
                {
                    LOG_WRN("settings: %s takes no value; ignoring it", name.c_str());
                    continue;
                }
                out.*(flag->field) = true;
                continue;
            }

            const auto option = find_value_option(name);
            const post_field* post_option = option.has_value() ? nullptr : find_post_field_by_option(name);
            if (!option.has_value() && post_option == nullptr)
            {
                LOG_WRN("settings: ignoring unknown option '%s'", name.c_str());
                continue;
            }
            if (!value.has_value() && i + 1 < args.size() && args[i + 1] != nullptr && !looks_like_option(args[i + 1]))
            {
                ++i;
                value = std::string_view{args[i]};
            }
            if (!value.has_value() || trim(*value).empty())
            {
                LOG_WRN("settings: %s needs a value; ignoring it", name.c_str());
                continue;
            }

            // A post-processing option is validated against a scratch copy
            // (warning here, like every other option) and recorded in order,
            // so apply_command_line replays only values that parse and the
            // last valid occurrence wins.
            if (post_option != nullptr)
            {
                post_process_settings scratch;
                if (set_post_from_text(scratch, *post_option, *value, name.c_str()))
                {
                    out.post.push_back({std::string{post_option->key}, std::string{trim(*value)}});
                }
                continue;
            }

            switch (*option)
            {
            case value_option::width:
                store_if(out.width, parse_unsigned_or_warn(name.c_str(), *value, 0, k_max_window_dimension));
                break;
            case value_option::height:
                store_if(out.height, parse_unsigned_or_warn(name.c_str(), *value, 0, k_max_window_dimension));
                break;
            case value_option::backend:
                store_if(out.backend, parse_graphics_backend_or_warn(name.c_str(), *value));
                break;
            case value_option::vsync:
                store_if(out.vsync, parse_bool_or_warn(name.c_str(), *value));
                break;
            case value_option::frames_in_flight:
                store_if(out.frames_in_flight,
                         parse_unsigned_or_warn(name.c_str(), *value, 1, graphics_settings::max_frames_in_flight));
                break;
            case value_option::parallel_draw_threshold:
                store_if(out.parallel_draw_threshold,
                         parse_unsigned_or_warn(name.c_str(), *value, 0, k_max_parallel_draw_threshold));
                break;
            case value_option::shadow_resolution:
                store_if(
                    out.shadow_resolution,
                    parse_unsigned_or_warn(name.c_str(), *value, k_min_shadow_resolution, k_max_shadow_resolution));
                break;
            case value_option::shadow_distance:
                store_if(out.shadow_distance,
                         parse_float_or_warn(name.c_str(), *value, k_min_shadow_distance, k_max_shadow_distance));
                break;
            case value_option::shadow_cascades:
                store_if(out.shadow_cascades,
                         parse_unsigned_or_warn(name.c_str(), *value, 1, shadow_settings::max_cascade_count));
                break;
            case value_option::shadow_bias:
                store_if(out.shadow_bias, parse_float_or_warn(name.c_str(), *value, 0.0f, k_max_shadow_bias));
                break;
            case value_option::shadow_slope_bias:
                store_if(out.shadow_slope_bias,
                         parse_float_or_warn(name.c_str(), *value, 0.0f, k_max_shadow_slope_bias));
                break;
            case value_option::shadow_pcf_kernel:
                store_if(out.shadow_pcf_kernel,
                         parse_unsigned_or_warn(name.c_str(), *value, 1, shadow_settings::max_pcf_kernel));
                break;
            case value_option::log_level:
                out.log_level = std::string{trim(*value)};
                break;
            case value_option::settings_path:
                out.settings_path = std::string{trim(*value)};
                break;
            case value_option::content_root:
                out.content_root = std::string{trim(*value)};
                break;
            case value_option::frame_limit:
                store_if(out.frame_limit, parse_unsigned_or_warn(name.c_str(), *value, 0, k_max_frame_limit));
                break;
            }
        }
        return out;
    }

    void apply_command_line(settings& out, const command_line_options& options)
    {
        assign_if(out.window.width, options.width);
        assign_if(out.window.height, options.height);
        assign_if(out.window.mode, options.mode);
        assign_if(out.window.vsync, options.vsync);
        assign_if(out.graphics.backend, options.backend);
        assign_if(out.graphics.frames_in_flight, options.frames_in_flight);
        assign_if(out.graphics.parallel_draw_threshold, options.parallel_draw_threshold);
        assign_if(out.shadows.resolution, options.shadow_resolution);
        assign_if(out.shadows.distance, options.shadow_distance);
        assign_if(out.shadows.cascade_count, options.shadow_cascades);
        assign_if(out.shadows.bias, options.shadow_bias);
        assign_if(out.shadows.slope_bias, options.shadow_slope_bias);
        assign_if(out.shadows.pcf_kernel, options.shadow_pcf_kernel);
        for (const post_option_value& option : options.post)
        {
            if (const post_field* field = find_post_field_by_key(option.key))
            {
                set_post_from_text(out.post, *field, option.value, option.key.c_str());
            }
        }
        assign_if(out.content.root, options.content_root);
        assign_if(out.diagnostics.frame_limit, options.frame_limit);
        if (options.fail_on_error)
        {
            out.diagnostics.fail_on_error = true;
        }
    }

    const char* command_line_usage() noexcept
    {
        return k_usage;
    }
} // namespace core
