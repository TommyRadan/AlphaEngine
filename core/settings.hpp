// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file settings.hpp
 * @brief Engine-wide configuration, grouped per subsystem domain.
 *
 * The values are resolved once at startup by @ref core::load_settings: compiled defaults, then
 * `<pref path>/settings.json`, then the `ALPHAENGINE_*` environment variables, then the command line, each
 * layer overriding the one before it. The per-layer steps are pure functions in core/settings_parse.hpp so
 * they can be exercised without touching the process environment or the platform.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace core
{
    /** @brief Presentation mode for the application window. */
    enum class window_mode
    {
        windowed,   /**< Standard decorated window. */
        fullscreen, /**< Fullscreen at the display's resolution. */
        borderless  /**< Borderless window. */
    };

    /** @brief GPU backend selected by @ref graphics_settings. */
    enum class graphics_backend
    {
        vulkan, /**< Vulkan. */
    };

    /** @brief Window / presentation configuration. */
    struct window_settings
    {
        /**
         * @brief Window size in logical points. Zero on either axis means "match the primary display": that
         *        query needs the window system, so @ref platform::window::init resolves it after bringing video
         *        up and writes the concrete size back here for every later reader.
         */
        unsigned int width{0};
        unsigned int height{0};
        std::string title;
        window_mode mode{window_mode::windowed};

        /**
         * @brief Whether the presentation engine should wait for vertical
         *        sync. Off by default in both configurations so the frame
         *        rate is uncapped (the debug FPS overlay is more useful with
         *        vsync off, and release favours latency over tearing).
         */
        bool vsync{false};

        /** @brief Whether @ref width or @ref height is still the "match the display" placeholder. */
        bool uses_native_resolution() const noexcept;

        /** @brief Returns @c width / @c height, or 1 while the size is unresolved (@ref uses_native_resolution). */
        float aspect_ratio() const noexcept;
    };

    /** @brief GPU backend configuration. */
    struct graphics_settings
    {
        /** @brief GPU backend the engine brings up at startup. Read once during @ref runtime::engine construction. */
        graphics_backend backend{graphics_backend::vulkan};

        /**
         * @brief Whether temporal anti-aliasing is enabled.
         *
         * When on, the scene pass jitters the projection matrix with a
         * Halton sub-pixel sequence and the @ref rendering_engine::taa_pass
         * accumulates the jittered frames into a stable, supersampled image
         * (a neighbourhood colour clamp keeps moving content from ghosting).
         * Read once during rendering-engine init.
         */
        bool temporal_aa{true};

        /**
         * @brief Whether the scene starts with the depth pre-pass enabled.
         *
         * When on, @ref rendering_engine::depth_prepass lays the opaque queue's depth into the scene target
         * front-to-back through each material's vertex stage alone, and the scene pass then loads that depth and
         * shades every pre-passed surface with depth writes off and a less-or-equal test, so each covered pixel
         * is shaded once. Off by default. Seeds @c rendering_engine::renderer at init; toggled at runtime with
         * @c renderer::set_depth_prepass.
         */
        bool depth_prepass{false};

        /** @brief Upper bound of @ref frames_in_flight; the Vulkan backend sizes its per-frame rings by it. */
        static constexpr unsigned int max_frames_in_flight = 2;

        /**
         * @brief Frames the Vulkan backend may have in flight at once, 1 to @ref max_frames_in_flight.
         *
         * At 1 the CPU waits for each frame's GPU work before recording the next; at 2 it records frame N+1
         * while the GPU still draws frame N, and every host-written buffer is double-buffered by the device so
         * the two never touch the same memory. Read once during @ref rendering_engine::gpu::device::init.
         */
        unsigned int frames_in_flight{2};

        /**
         * @brief Draw count above which the scene pass records a frame's draws in parallel, and the fewest
         *        draws one recording chunk holds.
         *
         * On the Vulkan backend a scene pass (or depth pre-pass) whose sorted draw list holds more than this
         * many draws is split into contiguous chunks of at least this many draws — at most one per recording
         * thread, the job pool's workers plus the main thread — and every chunk is recorded into its own
         * secondary command buffer at once; at or below it the pass records serially, so the fork-join
         * overhead is only paid where it is amortised. 0 disables the parallel path. Read once during
         * rendering-engine init.
         */
        unsigned int parallel_draw_threshold{512};
    };

    /** @brief Camera configuration. */
    struct camera_settings
    {
        /** @brief Vertical field of view of the perspective camera, in degrees. */
        float field_of_view{70.0f};
    };

    /** @brief Input / mouse configuration. */
    struct input_settings
    {
        /** @brief Mouse-look scale, radians per point of cursor travel. */
        float mouse_sensitivity{0.005f};
        bool mouse_reversed{false};

        /**
         * @brief Rebinds for @c core::input actions and axes, from the `input.bindings` section of settings.json
         *        (e.g. `"move_forward": ["key:w", "gamepad_axis:left_y-"]`). Keyed by the action or axis name; a
         *        name registered through @c core::input::bind_action / @c bind_axis with an entry here uses
         *        these binding strings instead of its compiled defaults. Empty by default. See @c core/input.hpp
         *        for the binding-string grammar; an entry that does not parse is warned about and skipped.
         */
        std::unordered_map<std::string, std::vector<std::string>> bindings;
    };

    /**
     * @brief Shadow-map configuration, read once during rendering-engine init.
     *
     * The directional light casts through cascaded shadow maps: the camera's view depth, from its near plane out
     * to @ref distance, is split into @ref cascade_count slices (a log / uniform blend), each rendered into its
     * own layer of one depth-array texture. The spot and omni passes take their map size from @ref resolution
     * and their rasterizer slope bias from @ref slope_bias as well.
     */
    struct shadow_settings
    {
        /** @brief Upper bound of @ref cascade_count; the lit shaders' shadow block holds this many matrices. */
        static constexpr unsigned int max_cascade_count = 4;

        /** @brief Upper bound of @ref pcf_kernel. */
        static constexpr unsigned int max_pcf_kernel = 8;

        /**
         * @brief Edge length, in texels, of each directional cascade's square depth map and of the spot map;
         *        each omni cube face gets half of it. Clamped to the device's texture-size limit.
         */
        unsigned int resolution{2048};

        /** @brief View depth, in world units, the directional cascades cover in front of the camera. */
        float distance{25.0f};

        /** @brief Number of directional cascades, 1 to @ref max_cascade_count. */
        unsigned int cascade_count{max_cascade_count};

        /**
         * @brief Receiver-side depth-comparison bias of the directional shadow, as a fraction of a cascade's
         *        reference light-box depth, so it grows with each cascade's size; the lit shader scales it up on
         *        surfaces that turn away from the light.
         */
        float bias{0.0015f};

        /** @brief Slope factor of the rasterizer depth bias every shadow pass renders its casters with. */
        float slope_bias{1.5f};

        /**
         * @brief Hardware-filtered PCF taps per side of the directional shadow kernel: 1 is a single bilinear
         *        comparison over 2x2 texels, and n taps one texel apart span (n + 1) x (n + 1) texels.
         */
        unsigned int pcf_kernel{4};
    };

    /** @brief Tonemap curve selected by @ref post_process_settings::tonemap. */
    enum class tonemap_curve
    {
        none,     /**< No curve: the exposed colour is only clamped and gamma encoded. */
        reinhard, /**< Reinhard's x / (1 + x) shoulder. */
        aces,     /**< Krzysztof Narkowicz's ACES filmic approximation. */
    };

    /**
     * @brief Startup values of the post-processing chain, read once during rendering-engine init.
     *
     * Seeds @c rendering_engine::post_settings, which the renderer (and the debug overlay's Post panel) then tunes
     * live through @c rendering_engine::renderer::set_post_settings; nothing is written back here. The defaults match
     * @c rendering_engine::post_settings' own, so an absent `post` section renders exactly as before. Whether the
     * temporal-AA pass exists at all stays @ref graphics_settings::temporal_aa; only its feedback weight lives here.
     */
    struct post_process_settings
    {
        /** @brief Manual pre-curve exposure scale; ignored while @ref auto_exposure is on. */
        float exposure{1.0f};

        /** @brief Tonemap curve applied after the exposure. */
        tonemap_curve tonemap{tonemap_curve::aces};

        /** @brief Whether the HDR bloom glow runs. */
        bool bloom{true};

        /** @brief HDR luminance above which pixels start to bloom. */
        float bloom_threshold{1.0f};

        /** @brief Soft-knee width of the bloom bright pass, as a fraction of @ref bloom_threshold. */
        float bloom_knee{0.5f};

        /** @brief Overall bloom glow strength blended back into the scene colour. */
        float bloom_strength{0.6f};

        /** @brief Steady-state weight of the temporal-AA history in its resolve blend. */
        float taa_feedback{0.9f};

        /** @brief Whether FXAA smooths the final image (off degrades the pass to a straight copy). */
        bool fxaa{true};

        /** @brief Whether the volumetric fog pass raymarches the scene's height fog. */
        bool volumetric_fog{false};

        /** @brief Multiplier on the height-fog density the volumetric pass marches. */
        float volumetric_fog_density_scale{1.0f};

        /** @brief Henyey-Greenstein anisotropy of the volumetric fog's phase function, in (-1, 1). */
        float volumetric_fog_anisotropy{0.2f};

        /** @brief World-space distance from the camera the volumetric march stops at. */
        float volumetric_fog_max_distance{64.0f};

        /** @brief Volumetric march steps per ray. */
        unsigned int volumetric_fog_steps{32};

        /** @brief Scale on the volumetric fog's in-scattered light. */
        float volumetric_fog_intensity{1.0f};

        /**
         * @brief Colour-grading lookup table: an image path (resolved through the virtual filesystem like any
         *        other asset) of an N^2 x N strip LUT, applied to the display-referred colour after the tonemap
         *        curve and gamma encode. Empty (the default) turns grading off.
         */
        std::string grading_lut;

        /** @brief Blend between the ungraded (0) and the fully graded (1) colour. */
        float grading_intensity{1.0f};

        /** @brief Whether motion blur smears the HDR scene colour along the per-pixel motion vectors. */
        bool motion_blur{false};

        /** @brief Shutter scale: the fraction of a frame's motion the blur spans (0.5 is a 180-degree shutter). */
        float motion_blur_intensity{0.5f};

        /** @brief Taps along each pixel's motion vector. */
        unsigned int motion_blur_samples{8};

        /** @brief Longest blur, in pixels, a single pixel's motion may produce. */
        float motion_blur_max_radius{32.0f};

        /** @brief Whether eye adaptation drives the tonemap exposure instead of @ref exposure. */
        bool auto_exposure{false};

        /** @brief Lower bound of the metered scene brightness, in EV100 (brightens dark scenes up to here only). */
        float auto_exposure_min_ev{-4.0f};

        /** @brief Upper bound of the metered scene brightness, in EV100 (darkens bright scenes down to here only). */
        float auto_exposure_max_ev{16.0f};

        /** @brief Adaptation rate, per second, while the scene gets brighter. */
        float auto_exposure_speed_up{3.0f};

        /** @brief Adaptation rate, per second, while the scene gets darker. */
        float auto_exposure_speed_down{1.0f};

        /** @brief Stops added on top of the adapted exposure (positive brightens). */
        float auto_exposure_compensation{0.0f};
    };

    /** @brief Content location configuration. */
    struct content_settings
    {
        /**
         * @brief Directory the engine mounts as the root of the virtual filesystem (see core/vfs/vfs.hpp), so
         *        relative asset paths resolve under it. Empty (the default) means "discover it": the first
         *        @c content directory beside the executable or in one of its parents
         *        (@ref platform::content_root). Set from @c content.root in settings.json,
         *        @c ALPHAENGINE_CONTENT_ROOT or @c --content-root.
         */
        std::string root;
    };

    /**
     * @brief Bounded / fail-loud run configuration, read once by @ref runtime::engine.
     *
     * Exists for headless verification (a CI smoke run under the Vulkan validation layer): a frame limit turns
     * an otherwise-endless @c while(!is_quit_requested()) loop into one that exits on its own, and fail-on-error
     * turns a validation message or any other @c LOG_ERR into a non-zero process exit without the caller having
     * to grep the log.
     */
    struct diagnostics_settings
    {
        /**
         * @brief Frames to render before @ref runtime::engine requests a normal quit. 0 (the default) means run
         * forever. Only rendered frames count: a tick skipped while the window is minimized does not advance this
         * counter. Set from @c diagnostics.frame_limit in settings.json, @c ALPHAENGINE_FRAMES or @c --frames.
         */
        unsigned int frame_limit{0};

        /**
         * @brief Whether a @c LOG_ERR logged during the run makes the process exit non-zero, same as a @c LOG_FTL
         * always does. Off by default. Set from @c diagnostics.fail_on_error in settings.json,
         * @c ALPHAENGINE_FAIL_ON_ERROR or @c --fail-on-error (a presence-only flag on the command line).
         */
        bool fail_on_error{false};
    };

    /**
     * @brief Engine-wide configuration, owned by @ref runtime::engine.
     *
     * Plain data. The default constructor holds the compiled defaults (debug builds: 1600x900 windowed;
     * release builds: fullscreen at the display's native size); @ref load_settings layers the config file, the
     * environment and the command line on top. The engine takes the resolved struct by value at construction
     * and subsystems read it afterwards. Nothing writes to it once the engine is up, except the one documented
     * write-back of the resolved native size in @ref platform::window::init.
     */
    struct settings
    {
        settings();

        window_settings window;
        graphics_settings graphics;
        camera_settings camera;
        input_settings input;
        shadow_settings shadows;
        post_process_settings post;
        content_settings content;
        diagnostics_settings diagnostics;
    };

    /** @brief The lowercase name of @p mode (`windowed`, `fullscreen`, `borderless`). */
    const char* window_mode_name(window_mode mode) noexcept;

    /** @brief The lowercase name of @p backend (`vulkan`). */
    const char* graphics_backend_name(graphics_backend backend) noexcept;

    /** @brief The lowercase name of @p curve (`none`, `reinhard`, `aces`). */
    const char* tonemap_curve_name(tonemap_curve curve) noexcept;

    /** @brief Outcome of @ref load_settings. */
    struct settings_load_result
    {
        settings values;

        /**
         * @brief @c --help (or @c -h) was on the command line: the caller prints @ref command_line_usage and
         *        exits instead of starting the engine. @ref values is left at the compiled defaults.
         */
        bool help_requested{false};
    };

    /**
     * @brief Resolves the process-wide settings: compiled defaults, then `settings.json` in the per-user preference
     *        directory (or the file named by @c --settings), then the `ALPHAENGINE_*` environment variables, then
     *        the command line. A missing or malformed file and any unrecognised value are logged and skipped, never
     *        fatal. Applies @c --log-level to @ref core::logging on the way and logs the resolved values once at
     *        INFO. Requires @c LOG_INIT to have run.
     * @param argc           Argument count as given to @c main.
     * @param argv           Arguments as given to @c main; @c argv[0] (the program name) is skipped.
     * @param pref_directory Returns the per-user preference directory, or an empty path when there is none (the
     *                       engine passes @c platform::pref_path("AlphaEngine", "AlphaEngine")). Called only when
     *                       the settings file is read from there: not for @c --help, nor with @c --settings.
     */
    settings_load_result
    load_settings(int argc, char* const argv[], const std::function<std::filesystem::path()>& pref_directory);
} // namespace core
