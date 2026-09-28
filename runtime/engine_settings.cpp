// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/engine_settings.hpp>

#include <core/log.hpp>
#include <core/settings.hpp>
#include <core/settings_registry.hpp>
#include <rendering_engine/settings_registration.hpp>

namespace runtime
{
    namespace
    {
        void register_content_and_diagnostics(core::settings_registry& registry,
                                              content_settings& content,
                                              diagnostics_settings& diagnostics)
        {
            core::typed_section<content_settings> content_section =
                core::add_typed_section(registry, "content", content);
            content_section.add_text("root", &content_settings::root)
                .with_env("ALPHAENGINE_CONTENT_ROOT")
                .with_cli("--content-root")
                .with_help("  --content-root <path>    directory relative asset paths resolve under\n");
            content_section.set_log_resolved(
                [](const content_settings& s) {
                    LOG_INF("Content settings resolved: content_root='%s'",
                            s.root.empty() ? "(discover)" : s.root.c_str());
                });

            core::typed_section<diagnostics_settings> diagnostics_section =
                core::add_typed_section(registry, "diagnostics", diagnostics);
            diagnostics_section.add_count("frame_limit", &diagnostics_settings::frame_limit, 0, 1'000'000'000u)
                .with_env("ALPHAENGINE_FRAMES")
                .with_cli("--frames")
                .with_help("  --frames <n>             quit after rendering n frames (0 = run forever, the "
                           "default)\n");
            diagnostics_section.add_flag("fail_on_error", &diagnostics_settings::fail_on_error)
                .with_env("ALPHAENGINE_FAIL_ON_ERROR")
                .with_cli("--fail-on-error", core::settings_cli_style::presence)
                .with_help("  --fail-on-error          exit non-zero if any [ERR] line is logged, not only "
                           "[FTL]\n");
            diagnostics_section.set_log_resolved(
                [](const diagnostics_settings& s)
                {
                    LOG_INF("Diagnostics settings: frames=%u%s fail_on_error=%s",
                            s.frame_limit,
                            s.frame_limit == 0 ? " (run forever)" : "",
                            core::on_off(s.fail_on_error));
                });
        }

        constexpr const char k_post_processing_header[] =
            "Post-processing options (settings.json \"post\" section; each key is also the\n"
            "ALPHAENGINE_<KEY> variable, e.g. --bloom-threshold, post.bloom_threshold and\n"
            "ALPHAENGINE_BLOOM_THRESHOLD):\n";
    } // namespace

    void register_engine_settings(core::settings_registry& registry, engine_settings& out)
    {
        platform::register_settings(registry, out.window);
        rendering_engine::register_settings(registry, out.graphics, out.camera, out.shadows);
        core::register_settings(registry, out.input);
        register_content_and_diagnostics(registry, out.content, out.diagnostics);
        // Registered last: post-processing's many options print as their own trailing --help block, after
        // every other section, matching the historical command-line layout (see register_post_settings).
        rendering_engine::register_post_settings(registry, out.post);
    }

    std::string settings_help_text(const core::settings_registry& registry)
    {
        std::string text = core::settings_help_preamble();
        text += registry.help_lines_for("window");
        text += registry.help_lines_for("graphics");
        text += registry.help_lines_for("shadows");
        text += registry.help_lines_for("input");
        text += registry.help_lines_for("content");
        text += registry.help_lines_for("diagnostics");
        text += core::settings_meta_options_help();
        text += "\n";
        text += k_post_processing_header;
        text += registry.help_lines_for("post");
        text += core::settings_help_epilogue();
        return text;
    }
} // namespace runtime
