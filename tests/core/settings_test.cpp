// Unit tests for core::settings resolution: the value parsers, the compiled
// defaults, the three pure layers (apply_json over a JSON string,
// apply_environment over a map-backed lookup, parse_command_line /
// apply_command_line over an argument array), their tolerance of bad input —
// which must be reported as warnings, never errors — and the layering order
// defaults < file < environment < command line. Nothing here touches the
// process environment, the file system or SDL; load_settings itself (the pref
// path and the real environment) is not exercised.

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <core/settings.hpp>
#include <core/settings_parse.hpp>

namespace
{
    using core::graphics_backend;
    using core::window_mode;
    using core::logging::verbosity;

    core::environment_getter environment_of(std::map<std::string, std::string> variables)
    {
        return [variables = std::move(variables)](const char* name) -> const char*
        {
            const auto it = variables.find(name);
            return it == variables.end() ? nullptr : it->second.c_str();
        };
    }

    core::command_line_options parse(const std::vector<const char*>& args)
    {
        return core::parse_command_line(args);
    }

    // Bad input is reported through the log, so the tests observe the ring
    // that core::logging::init feeds: every rejection must be a warning and
    // nothing may reach error or fatal.
    class settings_test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            static const bool initialised = []
            {
                core::logging::init(0, nullptr);
                return true;
            }();
            (void)initialised;

            core::logging::clear_category_levels();
            core::logging::set_level(verbosity::trace);
            core::logging::clear_recent_messages();
        }

        void TearDown() override
        {
            core::logging::clear_category_levels();
            core::logging::set_level(verbosity::info);
            core::logging::clear_recent_messages();
        }
    };

    std::size_t count_at(verbosity level)
    {
        std::size_t count = 0;
        for (const auto& entry : core::logging::recent_messages())
        {
            if (entry.level == level)
            {
                ++count;
            }
        }
        return count;
    }

    std::size_t warnings()
    {
        return count_at(verbosity::warn);
    }

    std::size_t errors_or_worse()
    {
        return count_at(verbosity::error) + count_at(verbosity::fatal);
    }

    void expect_same(const core::settings& a, const core::settings& b)
    {
        EXPECT_EQ(a.window.width, b.window.width);
        EXPECT_EQ(a.window.height, b.window.height);
        EXPECT_EQ(a.window.title, b.window.title);
        EXPECT_EQ(a.window.mode, b.window.mode);
        EXPECT_EQ(a.window.double_buffered, b.window.double_buffered);
        EXPECT_EQ(a.window.vsync, b.window.vsync);
        EXPECT_EQ(a.graphics.backend, b.graphics.backend);
        EXPECT_EQ(a.graphics.temporal_aa, b.graphics.temporal_aa);
        EXPECT_FLOAT_EQ(a.camera.field_of_view, b.camera.field_of_view);
        EXPECT_FLOAT_EQ(a.input.mouse_sensitivity, b.input.mouse_sensitivity);
        EXPECT_EQ(a.input.mouse_reversed, b.input.mouse_reversed);
    }
} // namespace

// -- Scalar parsers ----------------------------------------------------------

TEST(settings_parsers, parse_bool_accepts_every_documented_spelling_ignoring_case_and_whitespace)
{
    for (const char* word : {"true", "1", "yes", "on", "TRUE", "Yes", " on ", "\tON\n"})
    {
        EXPECT_EQ(core::parse_bool(word), std::optional<bool>{true}) << word;
    }
    for (const char* word : {"false", "0", "no", "off", "FALSE", "No", " off ", "Off"})
    {
        EXPECT_EQ(core::parse_bool(word), std::optional<bool>{false}) << word;
    }
}

TEST(settings_parsers, parse_bool_rejects_anything_else)
{
    for (const char* word : {"", " ", "maybe", "2", "tru", "yes please", "on/off"})
    {
        EXPECT_FALSE(core::parse_bool(word).has_value()) << word;
    }
}

TEST(settings_parsers, parse_unsigned_parses_decimal_within_the_range)
{
    EXPECT_EQ(core::parse_unsigned("800", 0, 16384), std::optional<unsigned int>{800u});
    EXPECT_EQ(core::parse_unsigned(" 0 ", 0, 16384), std::optional<unsigned int>{0u});
    EXPECT_EQ(core::parse_unsigned("16384", 0, 16384), std::optional<unsigned int>{16384u});
    EXPECT_EQ(core::parse_unsigned("5", 5, 5), std::optional<unsigned int>{5u});
}

TEST(settings_parsers, parse_unsigned_rejects_signs_junk_and_out_of_range_values)
{
    for (const char* text : {"", " ", "-1", "+5", "abc", "12abc", "1.5", "0x10", "16385", "99999999999999"})
    {
        EXPECT_FALSE(core::parse_unsigned(text, 0, 16384).has_value()) << text;
    }
    EXPECT_FALSE(core::parse_unsigned("4", 5, 10).has_value());
}

TEST(settings_parsers, parse_float_parses_decimal_within_the_range_in_the_c_locale)
{
    EXPECT_EQ(core::parse_float("70", 1.0f, 179.0f), std::optional<float>{70.0f});
    EXPECT_EQ(core::parse_float(" 70.5 ", 1.0f, 179.0f), std::optional<float>{70.5f});
    EXPECT_EQ(core::parse_float("1e1", 1.0f, 179.0f), std::optional<float>{10.0f});
    EXPECT_EQ(core::parse_float("0.005", 0.0001f, 10.0f), std::optional<float>{0.005f});
}

TEST(settings_parsers, parse_float_rejects_junk_non_finite_and_out_of_range_values)
{
    for (const char* text : {"", "abc", "1.5x", "inf", "nan", "0.5", "180", "1e999"})
    {
        EXPECT_FALSE(core::parse_float(text, 1.0f, 179.0f).has_value()) << text;
    }
}

TEST(settings_parsers, mode_and_backend_names_parse_ignoring_case_and_round_trip)
{
    EXPECT_EQ(core::parse_window_mode("windowed"), std::optional<window_mode>{window_mode::windowed});
    EXPECT_EQ(core::parse_window_mode(" FULLSCREEN "), std::optional<window_mode>{window_mode::fullscreen});
    EXPECT_EQ(core::parse_window_mode("Borderless"), std::optional<window_mode>{window_mode::borderless});
    EXPECT_FALSE(core::parse_window_mode("huge").has_value());
    EXPECT_FALSE(core::parse_window_mode("").has_value());

    EXPECT_EQ(core::parse_graphics_backend("OpenGL"), std::optional<graphics_backend>{graphics_backend::opengl});
    EXPECT_EQ(core::parse_graphics_backend(" vulkan"), std::optional<graphics_backend>{graphics_backend::vulkan});
    EXPECT_FALSE(core::parse_graphics_backend("directx").has_value());

    for (const window_mode mode : {window_mode::windowed, window_mode::fullscreen, window_mode::borderless})
    {
        EXPECT_EQ(core::parse_window_mode(core::window_mode_name(mode)), std::optional<window_mode>{mode});
    }
    for (const graphics_backend backend : {graphics_backend::opengl, graphics_backend::vulkan})
    {
        EXPECT_EQ(core::parse_graphics_backend(core::graphics_backend_name(backend)),
                  std::optional<graphics_backend>{backend});
    }
}

// -- Defaults ----------------------------------------------------------------

TEST(settings_defaults, match_the_build_configuration)
{
    const core::settings s;
#ifdef _DEBUG
    EXPECT_EQ(s.window.width, 1600u);
    EXPECT_EQ(s.window.height, 900u);
    EXPECT_EQ(s.window.mode, window_mode::windowed);
    EXPECT_FALSE(s.window.uses_native_resolution());
#else
    EXPECT_EQ(s.window.width, 0u);
    EXPECT_EQ(s.window.height, 0u);
    EXPECT_EQ(s.window.mode, window_mode::fullscreen);
    EXPECT_TRUE(s.window.uses_native_resolution());
#endif
    EXPECT_EQ(s.window.title.rfind("AlphaEngine v", 0), 0u);
    EXPECT_TRUE(s.window.double_buffered);
    EXPECT_FALSE(s.window.vsync);
    EXPECT_EQ(s.graphics.backend, graphics_backend::vulkan);
    EXPECT_TRUE(s.graphics.temporal_aa);
    EXPECT_FLOAT_EQ(s.camera.field_of_view, 70.0f);
    EXPECT_FLOAT_EQ(s.input.mouse_sensitivity, 0.005f);
    EXPECT_FALSE(s.input.mouse_reversed);
}

TEST(settings_defaults, aspect_ratio_guards_an_unresolved_size)
{
    core::window_settings w;
    w.width = 1920;
    w.height = 1080;
    EXPECT_FALSE(w.uses_native_resolution());
    EXPECT_FLOAT_EQ(w.aspect_ratio(), 1920.0f / 1080.0f);

    w.height = 0;
    EXPECT_TRUE(w.uses_native_resolution());
    EXPECT_FLOAT_EQ(w.aspect_ratio(), 1.0f);
}

// -- JSON layer --------------------------------------------------------------

TEST_F(settings_test, apply_json_applies_every_documented_key)
{
    core::settings s;
    const bool ok = core::apply_json(s, R"({
        "window": {"width": 1920, "height": 1080, "mode": "Borderless", "vsync": true, "title": "Test"},
        "graphics": {"backend": "OpenGL", "temporal_aa": false},
        "camera": {"fov_degrees": 90},
        "input": {"mouse_sensitivity": 0.01, "mouse_reversed": true}
    })");

    EXPECT_TRUE(ok);
    EXPECT_EQ(s.window.width, 1920u);
    EXPECT_EQ(s.window.height, 1080u);
    EXPECT_EQ(s.window.mode, window_mode::borderless);
    EXPECT_TRUE(s.window.vsync);
    EXPECT_EQ(s.window.title, "Test");
    EXPECT_EQ(s.graphics.backend, graphics_backend::opengl);
    EXPECT_FALSE(s.graphics.temporal_aa);
    EXPECT_FLOAT_EQ(s.camera.field_of_view, 90.0f);
    EXPECT_FLOAT_EQ(s.input.mouse_sensitivity, 0.01f);
    EXPECT_TRUE(s.input.mouse_reversed);
    EXPECT_EQ(warnings(), 0u);
    EXPECT_EQ(errors_or_worse(), 0u);
}

TEST_F(settings_test, apply_json_applies_only_the_keys_present)
{
    core::settings s;
    const core::settings before = s;

    EXPECT_TRUE(core::apply_json(s, R"({"window": {"vsync": true}, "camera": {}})"));

    EXPECT_TRUE(s.window.vsync);
    EXPECT_EQ(s.window.width, before.window.width);
    EXPECT_EQ(s.window.height, before.window.height);
    EXPECT_EQ(s.window.mode, before.window.mode);
    EXPECT_EQ(s.window.title, before.window.title);
    EXPECT_EQ(s.graphics.backend, before.graphics.backend);
    EXPECT_FLOAT_EQ(s.camera.field_of_view, before.camera.field_of_view);
    EXPECT_EQ(warnings(), 0u);

    // An empty object is a valid, empty configuration.
    EXPECT_TRUE(core::apply_json(s, "{}"));
    EXPECT_EQ(warnings(), 0u);
}

TEST_F(settings_test, apply_json_rejects_malformed_documents_without_touching_the_settings)
{
    for (const char* text : {"", "   ", "{", "{\"window\": }", "[1, 2]", "42", "\"text\"", "null"})
    {
        core::logging::clear_recent_messages();
        core::settings s;
        const core::settings before = s;

        EXPECT_FALSE(core::apply_json(s, text)) << text;
        expect_same(s, before);
        EXPECT_EQ(warnings(), 1u) << text;
        EXPECT_EQ(errors_or_worse(), 0u) << text;
    }
}

TEST_F(settings_test, apply_json_skips_wrong_types_out_of_range_values_and_unknown_keys_with_warnings)
{
    core::settings s;
    const core::settings before = s;

    EXPECT_TRUE(core::apply_json(s, R"({
        "window": {"width": -5, "height": 1.5, "mode": "huge", "vsync": "yes", "title": 3, "widht": 800},
        "graphics": {"backend": 7, "temporal_aa": "off"},
        "camera": {"fov_degrees": 400},
        "input": {"mouse_sensitivity": 0, "mouse_reversed": 1},
        "shadows": {"enabled": true},
        "audio": 1
    })"));

    expect_same(s, before);
    // -5, 1.5, huge, "yes", 3, widht, 7, "off", 400, 0, 1, shadows, audio.
    EXPECT_EQ(warnings(), 13u);
    EXPECT_EQ(errors_or_worse(), 0u);
}

TEST_F(settings_test, apply_json_enforces_the_dimension_range_and_accepts_zero_as_native)
{
    core::settings s;
    EXPECT_TRUE(core::apply_json(s, R"({"window": {"width": 0, "height": 16385}})"));
    EXPECT_EQ(s.window.width, 0u);
    EXPECT_TRUE(s.window.uses_native_resolution());
    EXPECT_NE(s.window.height, 16385u);
    EXPECT_EQ(warnings(), 1u);
}

// -- Environment layer -------------------------------------------------------

TEST_F(settings_test, apply_environment_reads_every_variable_ignoring_case_and_whitespace)
{
    core::settings s;
    core::apply_environment(s,
                            environment_of({
                                {"ALPHAENGINE_WIDTH", " 1024 "},
                                {"ALPHAENGINE_HEIGHT", "768"},
                                {"ALPHAENGINE_WINDOW_MODE", "FULLSCREEN"},
                                {"ALPHAENGINE_VSYNC", "Yes"},
                                {"ALPHAENGINE_GRAPHICS_BACKEND", "OpenGL"},
                                {"ALPHAENGINE_TAA", "off"},
                            }));

    EXPECT_EQ(s.window.width, 1024u);
    EXPECT_EQ(s.window.height, 768u);
    EXPECT_EQ(s.window.mode, window_mode::fullscreen);
    EXPECT_TRUE(s.window.vsync);
    EXPECT_EQ(s.graphics.backend, graphics_backend::opengl);
    EXPECT_FALSE(s.graphics.temporal_aa);
    EXPECT_EQ(warnings(), 0u);
    EXPECT_EQ(errors_or_worse(), 0u);
}

TEST_F(settings_test, apply_environment_ignores_unset_empty_and_invalid_values)
{
    core::settings s;
    const core::settings before = s;

    core::apply_environment(s,
                            environment_of({
                                {"ALPHAENGINE_WIDTH", "wide"},
                                {"ALPHAENGINE_HEIGHT", ""},
                                {"ALPHAENGINE_WINDOW_MODE", "   "},
                                {"ALPHAENGINE_VSYNC", "maybe"},
                                {"ALPHAENGINE_GRAPHICS_BACKEND", "directx"},
                                {"ALPHAENGINE_TAA", "2"},
                                {"ALPHAENGINE_UNRELATED", "1"},
                            }));

    expect_same(s, before);
    // wide, maybe, directx, 2 — blank values are "not configured", not faults.
    EXPECT_EQ(warnings(), 4u);
    EXPECT_EQ(errors_or_worse(), 0u);

    // A getter that knows nothing leaves everything alone, quietly.
    core::apply_environment(s, environment_of({}));
    core::apply_environment(s, nullptr);
    expect_same(s, before);
    EXPECT_EQ(warnings(), 4u);
}

// -- Command line ------------------------------------------------------------

TEST_F(settings_test, command_line_accepts_both_the_separate_and_the_equals_value_forms)
{
    const auto options = parse({"--width", "800", "--height=600", "--backend=opengl", "--vsync", "on"});

    EXPECT_EQ(options.width, std::optional<unsigned int>{800u});
    EXPECT_EQ(options.height, std::optional<unsigned int>{600u});
    EXPECT_EQ(options.backend, std::optional<graphics_backend>{graphics_backend::opengl});
    EXPECT_EQ(options.vsync, std::optional<bool>{true});
    EXPECT_FALSE(options.mode.has_value());
    EXPECT_FALSE(options.help_requested);
    EXPECT_EQ(warnings(), 0u);
    EXPECT_EQ(errors_or_worse(), 0u);
}

TEST_F(settings_test, command_line_recognises_every_option)
{
    const auto options = parse({"--fullscreen", "--log-level", "info,gpu=trace", "--settings=/tmp/x.json", "--help"});

    EXPECT_EQ(options.mode, std::optional<window_mode>{window_mode::fullscreen});
    EXPECT_EQ(options.log_level, std::optional<std::string>{"info,gpu=trace"});
    EXPECT_EQ(options.settings_path, std::optional<std::string>{"/tmp/x.json"});
    EXPECT_TRUE(options.help_requested);

    EXPECT_TRUE(parse({"-h"}).help_requested);
    EXPECT_EQ(parse({"--windowed"}).mode, std::optional<window_mode>{window_mode::windowed});
    EXPECT_EQ(parse({"--borderless"}).mode, std::optional<window_mode>{window_mode::borderless});
    EXPECT_EQ(parse({"--vsync=off"}).vsync, std::optional<bool>{false});
    EXPECT_EQ(parse({"--width", "0"}).width, std::optional<unsigned int>{0u});
    EXPECT_EQ(warnings(), 0u);
}

TEST_F(settings_test, command_line_tolerates_bad_input_and_keeps_parsing)
{
    const auto options = parse({"--bogus",
                                "--width",
                                "abc",
                                "--height",
                                "--vsync=maybe",
                                "--fullscreen=1",
                                "stray",
                                "--width=99999",
                                "--backend",
                                "vulkan",
                                "--height=720",
                                "--settings="});

    EXPECT_FALSE(options.width.has_value());
    EXPECT_EQ(options.height, std::optional<unsigned int>{720u});
    EXPECT_FALSE(options.vsync.has_value());
    EXPECT_FALSE(options.mode.has_value());
    EXPECT_EQ(options.backend, std::optional<graphics_backend>{graphics_backend::vulkan});
    EXPECT_FALSE(options.settings_path.has_value());
    EXPECT_FALSE(options.help_requested);
    // bogus, abc, missing height, maybe, fullscreen=1, stray, 99999, empty --settings.
    EXPECT_EQ(warnings(), 8u);
    EXPECT_EQ(errors_or_worse(), 0u);
}

TEST_F(settings_test, command_line_last_valid_occurrence_wins)
{
    const auto widths = parse({"--width", "640", "--width", "800", "--width", "nope"});
    EXPECT_EQ(widths.width, std::optional<unsigned int>{800u});

    const auto modes = parse({"--fullscreen", "--windowed"});
    EXPECT_EQ(modes.mode, std::optional<window_mode>{window_mode::windowed});
    EXPECT_EQ(warnings(), 1u);
}

TEST_F(settings_test, apply_command_line_applies_only_what_was_given)
{
    core::settings s;
    const core::settings before = s;

    core::command_line_options options;
    options.height = 480u;
    options.vsync = true;
    core::apply_command_line(s, options);

    EXPECT_EQ(s.window.height, 480u);
    EXPECT_TRUE(s.window.vsync);
    EXPECT_EQ(s.window.width, before.window.width);
    EXPECT_EQ(s.window.mode, before.window.mode);
    EXPECT_EQ(s.graphics.backend, before.graphics.backend);

    core::apply_command_line(s, core::command_line_options{});
    EXPECT_EQ(s.window.height, 480u);
    EXPECT_TRUE(s.window.vsync);
}

TEST_F(settings_test, command_line_usage_names_every_option)
{
    const std::string usage = core::command_line_usage();
    for (const char* option : {"--width",
                               "--height",
                               "--windowed",
                               "--fullscreen",
                               "--borderless",
                               "--backend",
                               "--vsync",
                               "--log-level",
                               "--settings",
                               "--help"})
    {
        EXPECT_NE(usage.find(option), std::string::npos) << option;
    }
}

// -- Layering ----------------------------------------------------------------

TEST_F(settings_test, layers_override_in_order_defaults_file_environment_command_line)
{
    core::settings s;

    EXPECT_TRUE(core::apply_json(s, R"({
        "window": {"width": 800, "height": 600, "mode": "windowed", "vsync": true},
        "graphics": {"backend": "opengl"},
        "camera": {"fov_degrees": 60}
    })"));
    core::apply_environment(s,
                            environment_of({
                                {"ALPHAENGINE_WIDTH", "1024"},
                                {"ALPHAENGINE_HEIGHT", "768"},
                                {"ALPHAENGINE_WINDOW_MODE", "borderless"},
                            }));
    core::apply_command_line(s, parse({"--width", "1280", "--fullscreen"}));

    EXPECT_EQ(s.window.width, 1280u);                     // command line beat the environment and the file
    EXPECT_EQ(s.window.height, 768u);                     // environment beat the file
    EXPECT_EQ(s.window.mode, window_mode::fullscreen);    // command line beat the environment
    EXPECT_TRUE(s.window.vsync);                          // file beat the default
    EXPECT_EQ(s.graphics.backend, graphics_backend::opengl);
    EXPECT_FLOAT_EQ(s.camera.field_of_view, 60.0f);
    EXPECT_EQ(warnings(), 0u);
    EXPECT_EQ(errors_or_worse(), 0u);
}
