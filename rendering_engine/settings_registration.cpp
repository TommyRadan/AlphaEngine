// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/settings_registration.hpp>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <core/log.hpp>
#include <core/settings_parse.hpp>
#include <core/settings_registry.hpp>
#include <rendering_engine/camera/camera_settings.hpp>
#include <rendering_engine/graphics_settings.hpp>
#include <rendering_engine/passes/shadow_settings.hpp>
#include <rendering_engine/post_process_settings.hpp>

namespace rendering_engine
{
    namespace
    {
        constexpr unsigned int k_max_parallel_draw_threshold = 1u << 20;
        constexpr float k_min_field_of_view = 1.0f;
        constexpr float k_max_field_of_view = 179.0f;
        constexpr unsigned int k_min_shadow_resolution = 256;
        constexpr unsigned int k_max_shadow_resolution = 8192;
        constexpr float k_min_shadow_distance = 1.0f;
        constexpr float k_max_shadow_distance = 10000.0f;
        constexpr float k_max_shadow_bias = 0.1f;
        constexpr float k_max_shadow_slope_bias = 16.0f;

        constexpr float k_max_exposure = 64.0f;
        constexpr float k_max_bloom_threshold = 64.0f;
        constexpr float k_max_bloom_strength = 8.0f;
        constexpr float k_max_taa_feedback = 0.99f;
        constexpr float k_max_volumetric_fog_density_scale = 64.0f;
        constexpr float k_max_volumetric_fog_anisotropy = 0.99f;
        constexpr float k_max_volumetric_fog_distance = 10000.0f;
        constexpr unsigned int k_max_volumetric_fog_steps = 128;
        constexpr float k_max_volumetric_fog_intensity = 64.0f;
        constexpr float k_max_motion_blur_intensity = 2.0f;
        constexpr unsigned int k_min_motion_blur_samples = 2;
        constexpr unsigned int k_max_motion_blur_samples = 32;
        constexpr float k_min_motion_blur_radius = 1.0f;
        constexpr float k_max_motion_blur_radius = 256.0f;
        constexpr float k_min_exposure_ev = -16.0f;
        constexpr float k_max_exposure_ev = 32.0f;
        constexpr float k_max_exposure_speed = 100.0f;
        constexpr float k_max_exposure_compensation = 16.0f;

        std::optional<graphics_backend> parse_graphics_backend(std::string_view text)
        {
            text = core::trim(text);
            if (core::equals_ignoring_case(text, graphics_backend_name(graphics_backend::vulkan)))
            {
                return graphics_backend::vulkan;
            }
            return std::nullopt;
        }

        std::optional<tonemap_curve> parse_tonemap_curve(std::string_view text)
        {
            text = core::trim(text);
            constexpr tonemap_curve k_all_curves[] = {
                tonemap_curve::none, tonemap_curve::reinhard, tonemap_curve::aces};
            for (const tonemap_curve curve : k_all_curves)
            {
                if (core::equals_ignoring_case(text, tonemap_curve_name(curve)))
                {
                    return curve;
                }
            }
            return std::nullopt;
        }

        // ALPHAENGINE_<KEY>: the field's environment variable, derived from its `post` key.
        std::string post_env_name(std::string_view key)
        {
            std::string name{"ALPHAENGINE_"};
            for (const char c : key)
            {
                name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            }
            return name;
        }

        // --<key> with dashes for underscores: the field's command-line option.
        std::string post_cli_flag(std::string_view key)
        {
            std::string name{"--"};
            for (const char c : key)
            {
                name.push_back(c == '_' ? '-' : c);
            }
            return name;
        }

        // Every post field is exposed on the environment and the command line under names mechanically
        // derived from its JSON key (see post_env_name / post_cli_flag); this applies that shared surface on
        // top of whichever add_* call registered the field itself.
        core::settings_field&
        with_post_surface(core::settings_field& field, std::string_view key, std::string help_line)
        {
            return field.with_env(post_env_name(key)).with_cli(post_cli_flag(key)).with_help(std::move(help_line));
        }
    } // namespace

    void register_settings(core::settings_registry& registry,
                           graphics_settings& graphics,
                           camera_settings& camera,
                           shadow_settings& shadows)
    {
        core::typed_section<graphics_settings> graphics_section =
            core::add_typed_section(registry, "graphics", graphics);
        graphics_section
            .add_choice(
                "backend",
                [](std::string_view s) { return parse_graphics_backend(s).has_value(); },
                [](graphics_settings& s, std::string_view text) { s.backend = *parse_graphics_backend(text); },
                [](const graphics_settings& s) -> std::string { return graphics_backend_name(s.backend); },
                "a supported graphics backend (vulkan)")
            .with_env("ALPHAENGINE_GRAPHICS_BACKEND")
            .with_cli("--backend")
            .with_help("  --backend <name>         gpu backend: vulkan\n");
        graphics_section.add_flag("temporal_aa", &graphics_settings::temporal_aa).with_env("ALPHAENGINE_TAA");
        graphics_section.add_flag("depth_prepass", &graphics_settings::depth_prepass)
            .with_env("ALPHAENGINE_DEPTH_PREPASS");
        graphics_section
            .add_count(
                "frames_in_flight", &graphics_settings::frames_in_flight, 1, graphics_settings::max_frames_in_flight)
            .with_env("ALPHAENGINE_FRAMES_IN_FLIGHT")
            .with_cli("--frames-in-flight")
            .with_help("  --frames-in-flight <n>   frames the vulkan backend keeps in flight, 1 to 2\n");
        graphics_section
            .add_count("parallel_draw_threshold",
                       &graphics_settings::parallel_draw_threshold,
                       0,
                       k_max_parallel_draw_threshold)
            .with_env("ALPHAENGINE_PARALLEL_DRAW_THRESHOLD")
            .with_cli("--parallel-draw-threshold")
            .with_help("  --parallel-draw-threshold <n>\n"
                       "                           draws above which the scene pass records in parallel\n"
                       "                           on vulkan (and per recording chunk); 0 disables\n");
        graphics_section.set_log_resolved(
            [](const graphics_settings& s)
            {
                LOG_INF("Graphics settings resolved: backend=%s temporal_aa=%s depth_prepass=%s "
                        "frames_in_flight=%u parallel_draw_threshold=%u",
                        graphics_backend_name(s.backend),
                        core::on_off(s.temporal_aa),
                        core::on_off(s.depth_prepass),
                        s.frames_in_flight,
                        s.parallel_draw_threshold);
            });

        core::typed_section<camera_settings> camera_section = core::add_typed_section(registry, "camera", camera);
        camera_section.add_number(
            "fov_degrees", &camera_settings::field_of_view, k_min_field_of_view, k_max_field_of_view);
        camera_section.set_log_resolved(
            [](const camera_settings& s)
            { LOG_INF("Camera settings resolved: fov=%.1f", static_cast<double>(s.field_of_view)); });

        core::typed_section<shadow_settings> shadows_section = core::add_typed_section(registry, "shadows", shadows);
        shadows_section
            .add_count("resolution", &shadow_settings::resolution, k_min_shadow_resolution, k_max_shadow_resolution)
            .with_env("ALPHAENGINE_SHADOW_RESOLUTION")
            .with_cli("--shadow-resolution")
            .with_help("  --shadow-resolution <n>  texels per side of the shadow maps\n");
        shadows_section.add_number("distance", &shadow_settings::distance, k_min_shadow_distance, k_max_shadow_distance)
            .with_env("ALPHAENGINE_SHADOW_DISTANCE")
            .with_cli("--shadow-distance")
            .with_help("  --shadow-distance <d>    view depth the directional cascades cover\n");
        shadows_section
            .add_count("cascade_count", &shadow_settings::cascade_count, 1, shadow_settings::max_cascade_count)
            .with_env("ALPHAENGINE_SHADOW_CASCADES")
            .with_cli("--shadow-cascades")
            .with_help("  --shadow-cascades <n>    directional shadow cascades, 1 to 4\n");
        shadows_section.add_number("bias", &shadow_settings::bias, 0.0f, k_max_shadow_bias)
            .with_env("ALPHAENGINE_SHADOW_BIAS")
            .with_cli("--shadow-bias")
            .with_help("  --shadow-bias <b>        directional receiver depth bias\n");
        shadows_section.add_number("slope_bias", &shadow_settings::slope_bias, 0.0f, k_max_shadow_slope_bias)
            .with_env("ALPHAENGINE_SHADOW_SLOPE_BIAS")
            .with_cli("--shadow-slope-bias")
            .with_help("  --shadow-slope-bias <s>  rasterizer slope bias of the shadow passes\n");
        shadows_section.add_count("pcf_kernel", &shadow_settings::pcf_kernel, 1, shadow_settings::max_pcf_kernel)
            .with_env("ALPHAENGINE_SHADOW_PCF_KERNEL")
            .with_cli("--shadow-pcf-kernel")
            .with_help("  --shadow-pcf-kernel <n>  hardware pcf taps per side, 1 to 8\n");
        shadows_section.set_log_resolved(
            [](const shadow_settings& s)
            {
                LOG_INF(
                    "Shadow settings: resolution=%u distance=%.1f cascades=%u bias=%.5f slope_bias=%.2f pcf_kernel=%u",
                    s.resolution,
                    static_cast<double>(s.distance),
                    s.cascade_count,
                    static_cast<double>(s.bias),
                    static_cast<double>(s.slope_bias),
                    s.pcf_kernel);
            });
    }

    void register_post_settings(core::settings_registry& registry, post_process_settings& post)
    {
        core::typed_section<post_process_settings> section = core::add_typed_section(registry, "post", post);

        with_post_surface(section.add_number("exposure", &post_process_settings::exposure, 0.0f, k_max_exposure),
                          "exposure",
                          "  --exposure <e>                        manual exposure scale\n");
        with_post_surface(
            section.add_choice(
                "tonemap",
                [](std::string_view s) { return parse_tonemap_curve(s).has_value(); },
                [](post_process_settings& s, std::string_view text) { s.tonemap = *parse_tonemap_curve(text); },
                [](const post_process_settings& s) -> std::string { return tonemap_curve_name(s.tonemap); },
                "one of none|reinhard|aces"),
            "tonemap",
            "  --tonemap <curve>                     tonemap curve: none, reinhard or aces\n");
        with_post_surface(section.add_flag("bloom", &post_process_settings::bloom),
                          "bloom",
                          "  --bloom <on|off>                      hdr bloom glow\n");
        with_post_surface(
            section.add_number("bloom_threshold", &post_process_settings::bloom_threshold, 0.0f, k_max_bloom_threshold),
            "bloom_threshold",
            "  --bloom-threshold <t>                 luminance above which pixels bloom\n");
        with_post_surface(section.add_number("bloom_knee", &post_process_settings::bloom_knee, 0.0f, 1.0f),
                          "bloom_knee",
                          "  --bloom-knee <k>                      bloom soft knee, 0 to 1\n");
        with_post_surface(
            section.add_number("bloom_strength", &post_process_settings::bloom_strength, 0.0f, k_max_bloom_strength),
            "bloom_strength",
            "  --bloom-strength <s>                  bloom glow strength\n");
        with_post_surface(
            section.add_number("taa_feedback", &post_process_settings::taa_feedback, 0.0f, k_max_taa_feedback),
            "taa_feedback",
            "  --taa-feedback <f>                    temporal-aa history weight, 0 to 0.99\n");
        with_post_surface(section.add_flag("fxaa", &post_process_settings::fxaa),
                          "fxaa",
                          "  --fxaa <on|off>                       fast approximate anti-aliasing\n");
        with_post_surface(section.add_flag("volumetric_fog", &post_process_settings::volumetric_fog),
                          "volumetric_fog",
                          "  --volumetric-fog <on|off>             raymarched height fog\n");
        with_post_surface(section.add_number("volumetric_fog_density_scale",
                                             &post_process_settings::volumetric_fog_density_scale,
                                             0.0f,
                                             k_max_volumetric_fog_density_scale),
                          "volumetric_fog_density_scale",
                          "  --volumetric-fog-density-scale <d>    volumetric fog density multiplier\n");
        with_post_surface(section.add_number("volumetric_fog_anisotropy",
                                             &post_process_settings::volumetric_fog_anisotropy,
                                             -k_max_volumetric_fog_anisotropy,
                                             k_max_volumetric_fog_anisotropy),
                          "volumetric_fog_anisotropy",
                          "  --volumetric-fog-anisotropy <g>       volumetric phase anisotropy, -0.99 to 0.99\n");
        with_post_surface(section.add_number("volumetric_fog_max_distance",
                                             &post_process_settings::volumetric_fog_max_distance,
                                             0.0f,
                                             k_max_volumetric_fog_distance),
                          "volumetric_fog_max_distance",
                          "  --volumetric-fog-max-distance <d>     distance the volumetric march stops at\n");
        with_post_surface(
            section.add_count(
                "volumetric_fog_steps", &post_process_settings::volumetric_fog_steps, 1, k_max_volumetric_fog_steps),
            "volumetric_fog_steps",
            "  --volumetric-fog-steps <n>            volumetric march steps, 1 to 128\n");
        with_post_surface(section.add_number("volumetric_fog_intensity",
                                             &post_process_settings::volumetric_fog_intensity,
                                             0.0f,
                                             k_max_volumetric_fog_intensity),
                          "volumetric_fog_intensity",
                          "  --volumetric-fog-intensity <i>        volumetric in-scattering scale\n");
        with_post_surface(section.add_text("grading_lut", &post_process_settings::grading_lut),
                          "grading_lut",
                          "  --grading-lut <path>                  colour-grading strip lut (n*n x n)\n");
        with_post_surface(
            section.add_number("grading_intensity", &post_process_settings::grading_intensity, 0.0f, 1.0f),
            "grading_intensity",
            "  --grading-intensity <i>               colour-grading blend, 0 to 1\n");
        with_post_surface(section.add_flag("motion_blur", &post_process_settings::motion_blur),
                          "motion_blur",
                          "  --motion-blur <on|off>                motion blur from the velocity buffer\n");
        with_post_surface(section.add_number("motion_blur_intensity",
                                             &post_process_settings::motion_blur_intensity,
                                             0.0f,
                                             k_max_motion_blur_intensity),
                          "motion_blur_intensity",
                          "  --motion-blur-intensity <s>           shutter scale, 0 to 2\n");
        with_post_surface(section.add_count("motion_blur_samples",
                                            &post_process_settings::motion_blur_samples,
                                            k_min_motion_blur_samples,
                                            k_max_motion_blur_samples),
                          "motion_blur_samples",
                          "  --motion-blur-samples <n>             taps per pixel, 2 to 32\n");
        with_post_surface(section.add_number("motion_blur_max_radius",
                                             &post_process_settings::motion_blur_max_radius,
                                             k_min_motion_blur_radius,
                                             k_max_motion_blur_radius),
                          "motion_blur_max_radius",
                          "  --motion-blur-max-radius <px>         longest blur in pixels, 1 to 256\n");
        with_post_surface(section.add_flag("auto_exposure", &post_process_settings::auto_exposure),
                          "auto_exposure",
                          "  --auto-exposure <on|off>              eye adaptation instead of --exposure\n");
        with_post_surface(section.add_number("auto_exposure_min_ev",
                                             &post_process_settings::auto_exposure_min_ev,
                                             k_min_exposure_ev,
                                             k_max_exposure_ev),
                          "auto_exposure_min_ev",
                          "  --auto-exposure-min-ev <ev>           lowest metered brightness, in ev100\n");
        with_post_surface(section.add_number("auto_exposure_max_ev",
                                             &post_process_settings::auto_exposure_max_ev,
                                             k_min_exposure_ev,
                                             k_max_exposure_ev),
                          "auto_exposure_max_ev",
                          "  --auto-exposure-max-ev <ev>           highest metered brightness, in ev100\n");
        with_post_surface(
            section.add_number(
                "auto_exposure_speed_up", &post_process_settings::auto_exposure_speed_up, 0.0f, k_max_exposure_speed),
            "auto_exposure_speed_up",
            "  --auto-exposure-speed-up <r>          adaptation rate toward brighter\n");
        with_post_surface(section.add_number("auto_exposure_speed_down",
                                             &post_process_settings::auto_exposure_speed_down,
                                             0.0f,
                                             k_max_exposure_speed),
                          "auto_exposure_speed_down",
                          "  --auto-exposure-speed-down <r>        adaptation rate toward darker\n");
        with_post_surface(section.add_number("auto_exposure_compensation",
                                             &post_process_settings::auto_exposure_compensation,
                                             -k_max_exposure_compensation,
                                             k_max_exposure_compensation),
                          "auto_exposure_compensation",
                          "  --auto-exposure-compensation <stops>  stops added to the adapted exposure\n");

        section.set_log_resolved(
            [](const post_process_settings& s)
            {
                LOG_INF("Post settings: exposure=%.2f tonemap=%s bloom=%s (threshold=%.2f knee=%.2f strength=%.2f) "
                        "taa_feedback=%.2f fxaa=%s volumetric_fog=%s (density_scale=%.2f anisotropy=%.2f "
                        "max_distance=%.1f steps=%u intensity=%.2f)",
                        static_cast<double>(s.exposure),
                        tonemap_curve_name(s.tonemap),
                        core::on_off(s.bloom),
                        static_cast<double>(s.bloom_threshold),
                        static_cast<double>(s.bloom_knee),
                        static_cast<double>(s.bloom_strength),
                        static_cast<double>(s.taa_feedback),
                        core::on_off(s.fxaa),
                        core::on_off(s.volumetric_fog),
                        static_cast<double>(s.volumetric_fog_density_scale),
                        static_cast<double>(s.volumetric_fog_anisotropy),
                        static_cast<double>(s.volumetric_fog_max_distance),
                        s.volumetric_fog_steps,
                        static_cast<double>(s.volumetric_fog_intensity));
                LOG_INF("Post settings: grading_lut='%s' grading_intensity=%.2f motion_blur=%s (intensity=%.2f "
                        "samples=%u max_radius=%.1f) auto_exposure=%s (ev=[%.1f, %.1f] speed_up=%.2f speed_down=%.2f "
                        "compensation=%.2f)",
                        s.grading_lut.empty() ? "(off)" : s.grading_lut.c_str(),
                        static_cast<double>(s.grading_intensity),
                        core::on_off(s.motion_blur),
                        static_cast<double>(s.motion_blur_intensity),
                        s.motion_blur_samples,
                        static_cast<double>(s.motion_blur_max_radius),
                        core::on_off(s.auto_exposure),
                        static_cast<double>(s.auto_exposure_min_ev),
                        static_cast<double>(s.auto_exposure_max_ev),
                        static_cast<double>(s.auto_exposure_speed_up),
                        static_cast<double>(s.auto_exposure_speed_down),
                        static_cast<double>(s.auto_exposure_compensation));
            });
    }
} // namespace rendering_engine
