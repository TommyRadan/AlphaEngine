// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <platform/window_settings.hpp>

#include <optional>
#include <string_view>

#include <core/log.hpp>
#include <core/settings_parse.hpp>
#include <core/settings_registry.hpp>
#include <core/version.hpp>

namespace platform
{
    namespace
    {
        /** @brief Largest window dimension accepted from any source; 0 means "match the display". */
        constexpr unsigned int k_max_window_dimension = 16384;

        std::optional<window_mode> parse_window_mode(std::string_view text)
        {
            text = core::trim(text);
            constexpr window_mode k_all_modes[] = {
                window_mode::windowed, window_mode::fullscreen, window_mode::borderless};
            for (const window_mode mode : k_all_modes)
            {
                if (core::equals_ignoring_case(text, window_mode_name(mode)))
                {
                    return mode;
                }
            }
            return std::nullopt;
        }
    } // namespace

    const char* window_mode_name(window_mode mode) noexcept
    {
        switch (mode)
        {
        case window_mode::windowed:
            return "windowed";
        case window_mode::fullscreen:
            return "fullscreen";
        case window_mode::borderless:
            return "borderless";
        }
        return "unknown";
    }

    window_settings::window_settings()
    {
#ifdef _DEBUG
        // Debug runs windowed and roomy: a larger canvas leaves space for the
        // ImGui debug panels (FPS overlay, settings inspector) without
        // crowding the scene.
        width = 1600;
        height = 900;
        mode = window_mode::windowed;
#else
        // Release goes fullscreen at the display's native size. The size is
        // left at zero here — the display cannot be queried before the
        // window system is up — and resolved by window::init once it is.
        width = 0;
        height = 0;
        mode = window_mode::fullscreen;
#endif
        title = std::string{"AlphaEngine v"} + core::version::get_version();
    }

    bool window_settings::uses_native_resolution() const noexcept
    {
        return width == 0 || height == 0;
    }

    float window_settings::aspect_ratio() const noexcept
    {
        if (height == 0)
        {
            return 1.0f;
        }
        return static_cast<float>(width) / static_cast<float>(height);
    }

    void register_settings(core::settings_registry& registry, window_settings& out)
    {
        core::typed_section<window_settings> section = core::add_typed_section(registry, "window", out);

        section.add_count("width", &window_settings::width, 0, k_max_window_dimension)
            .with_env("ALPHAENGINE_WIDTH")
            .with_cli("--width")
            .with_help("  --width <n>              window width in points (0 = match the display)\n");

        section.add_count("height", &window_settings::height, 0, k_max_window_dimension)
            .with_env("ALPHAENGINE_HEIGHT")
            .with_cli("--height")
            .with_help("  --height <n>             window height in points (0 = match the display)\n");

        section
            .add_choice(
                "mode",
                [](std::string_view s) { return parse_window_mode(s).has_value(); },
                [](window_settings& s, std::string_view text) { s.mode = *parse_window_mode(text); },
                [](const window_settings& s) -> std::string { return window_mode_name(s.mode); },
                "one of windowed|fullscreen|borderless")
            .with_env("ALPHAENGINE_WINDOW_MODE")
            .with_choice_flags(
                {{"--windowed", "windowed"}, {"--fullscreen", "fullscreen"}, {"--borderless", "borderless"}})
            .with_help("  --windowed               decorated window\n"
                       "  --fullscreen             fullscreen at the display's resolution\n"
                       "  --borderless             borderless window\n");

        section.add_flag("vsync", &window_settings::vsync)
            .with_env("ALPHAENGINE_VSYNC")
            .with_cli("--vsync")
            .with_help("  --vsync <on|off>         wait for vertical sync\n");

        section.add_text("title", &window_settings::title);

        section.set_log_resolved(
            [](const window_settings& s)
            {
                LOG_INF("Window settings resolved: size=%ux%u%s mode=%s vsync=%s title='%s'",
                        s.width,
                        s.height,
                        s.uses_native_resolution() ? " (match the display)" : "",
                        window_mode_name(s.mode),
                        core::on_off(s.vsync),
                        s.title.c_str());
            });
    }
} // namespace platform
