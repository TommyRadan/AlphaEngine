// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <string>

namespace rendering_engine
{
    /**
     * @brief Tonemap operator @ref post_settings::tonemap_op selects.
     *
     * The enumerator values are the contract with the fragment shader's
     * @c Tonemap UBO — they are uploaded verbatim and branched on at draw
     * time, so they must not be reordered.
     */
    enum class tonemap_operator : int
    {
        /// No curve: the exposed colour is only clamped and gamma encoded.
        none = 0,
        /// Reinhard's x / (1 + x) shoulder.
        reinhard = 1,
        /// Krzysztof Narkowicz's ACES filmic approximation. The default.
        aces = 2,
    };

    /**
     * @brief Runtime-tunable bloom parameters (@ref post_settings::bloom).
     *
     * The defaults match @ref bloom_pass's compiled-in values: a threshold
     * of 1.0 HDR luminance, a soft knee half the threshold wide, and an
     * overall glow strength of 0.6 spread across the blur pyramid's mips.
     * @ref bloom_pass::record rewrites its UBOs only when a field here
     * differs from what it last uploaded, and a resize (which rebuilds the
     * whole pyramid) rebakes from whatever was last applied rather than
     * these defaults.
     */
    struct bloom_settings
    {
        /// Runtime on/off switch. Disabling it does not remove the pass
        /// from the pass list — @ref bloom_pass::record simply skips its
        /// draws, leaving the HDR scene colour it would have brightened
        /// untouched.
        bool enabled{true};

        /// HDR luminance above which pixels start to bloom.
        float threshold{1.0f};

        /// Soft-knee width, as a fraction of @ref threshold: 0 is a hard
        /// cutoff at the threshold, larger values fade more pixels in
        /// gradually as they approach it.
        float knee{0.5f};

        /// Overall glow strength blended back into the scene colour,
        /// distributed across the blur pyramid's mips.
        float strength{0.6f};
    };

    /**
     * @brief Runtime-tunable temporal-AA parameters (@ref post_settings::taa).
     *
     * @ref enabled mirrors whether @ref taa_pass is actually in the pass
     * chain rather than requesting it: temporal AA also gates the scene
     * pass's projection jitter and is decided once, at @ref renderer::init,
     * from @c rendering_engine::graphics_settings::temporal_aa and the drawable size.
     * @ref renderer::set_post_settings overwrites whatever value it is
     * given here with the pass's real presence, so this always reports the
     * truth rather than silently failing to apply a request to flip it.
     *
     * @ref feedback is genuinely live: the steady-state weight of the
     * reprojected history in the resolve's blend (0 disables temporal
     * accumulation — every frame resolves from the current image alone;
     * closer to 1 keeps a longer, smoother but slower-to-settle history).
     * @ref taa_pass::record rewrites its UBO only when this differs from
     * the value it last uploaded.
     */
    struct taa_settings
    {
        bool enabled{true};
        float feedback{0.9f};
    };

    /**
     * @brief Runtime-tunable FXAA parameters (@ref post_settings::fxaa).
     *
     * @ref fxaa_pass always stays in the pass list — it is the pass that
     * writes the swapchain — so @ref enabled does not remove it: disabling
     * it bakes a zero edge step instead of the real one, which collapses
     * every off-centre tap onto the centre texel so the pass degrades to a
     * straight copy of its input, exactly like the degenerate-backbuffer
     * case it already handles at construction.
     */
    struct fxaa_settings
    {
        bool enabled{true};
    };

    /**
     * @brief Runtime-tunable volumetric fog parameters
     *        (@ref post_settings::volumetric).
     *
     * @ref volumetric_fog_pass raymarches the scene's exponential height
     * fog (@ref fog_settings::height_density, @c height_falloff and
     * @c reference_height, set through @ref renderer::set_fog) as a
     * participating medium lit by the scene lights, so the medium itself
     * is not duplicated here: with a height density of 0 there is nothing
     * to march and the pass draws nothing even while enabled. These fields
     * only tune how it is marched and lit. @ref volumetric_fog_pass::record
     * reads them every frame; there is no baked state to invalidate.
     *
     * Off by default, so the pass stays in the pass list but records no
     * draws and the image is exactly what it was without it.
     */
    struct volumetric_fog_settings
    {
        bool enabled{false};

        /// Multiplier on the height-fog density: the medium's extinction
        /// (and, at a white albedo, its scattering) coefficient per world
        /// unit is density_scale * the height fog's density at that point.
        float density_scale{1.0f};

        /// Henyey-Greenstein anisotropy g of the phase function, in
        /// (-1, 1): 0 scatters evenly in every direction, positive values
        /// scatter forward (bright halos looking toward a light), negative
        /// values back toward the light. Clamped to +-0.99 by the pass.
        float anisotropy{0.2f};

        /// World-space distance from the camera the march stops at; the
        /// scene beyond it keeps only the lit materials' analytic fog.
        float max_distance{64.0f};

        /// March steps per ray, clamped to [1, 128] by the pass. The
        /// per-pixel step jitter turns a low count's banding into noise
        /// that temporal AA averages away.
        int steps{32};

        /// Scale on the in-scattered light (not on the extinction).
        float intensity{1.0f};
    };

    /**
     * @brief Whether @p settings make @ref volumetric_fog_pass march at
     *        all: enabled, with a positive density scale and reach.
     *
     * The lit materials' analytic height fog starts at @c max_distance
     * instead of the camera exactly when this holds, so the stretch the
     * pass marches is not attenuated twice.
     */
    inline bool volumetric_fog_active(const volumetric_fog_settings& settings)
    {
        return settings.enabled && settings.density_scale > 0.0f && settings.max_distance > 0.0f;
    }

    /**
     * @brief Runtime-tunable colour grading (@ref post_settings::grading).
     *
     * @ref tonemap_pass applies a lookup table to the display-referred
     * colour it writes, after the tonemap curve and the gamma encode, and
     * blends the result with the ungraded colour by @ref intensity. The
     * table is an ordinary 2D image in the common "strip" layout (the
     * Unreal convention): N slices of N x N texels side by side, N^2 wide
     * and N high, black at the top-left; red rises left to right within a
     * slice, green rises top to bottom and blue rises slice by slice. The
     * identity table therefore maps every colour to itself, and a grade
     * authored in any image editor on top of it (or exported by a grading
     * tool at 16 or 32 texels per axis) reproduces that grade here.
     *
     * @ref renderer loads @ref lut through the asset cache as a
     * @c assets::color_space::linear texture (the stored values are already
     * the encoded output colours and must reach the shader unchanged) the
     * first frame after the path changes. An empty path, a table that
     * failed to load or is not N^2 x N, or an @ref intensity of 0 selects
     * the tonemap variant without the lookup, so grading costs nothing
     * while it is off.
     */
    struct color_grading_settings
    {
        /// Path of the strip LUT image, resolved through the virtual
        /// filesystem like any other asset. Empty turns grading off.
        std::string lut;

        /// Blend between the ungraded (0) and the fully graded (1) colour.
        float intensity{1.0f};
    };

    /**
     * @brief Runtime-tunable motion blur (@ref post_settings::motion_blur).
     *
     * @ref motion_blur_pass smears the HDR scene colour along each pixel's
     * motion vector from @ref velocity_pass, so it follows camera motion
     * over the static world (the velocity buffer does not model object
     * motion yet). Off by default: the pass then records nothing and the
     * passes after it read the scene colour directly.
     */
    struct motion_blur_settings
    {
        bool enabled{false};

        /// Shutter scale: the fraction of one frame's motion the blur
        /// spans. 0.5 is a 180-degree shutter, 1 smears across the whole
        /// frame-to-frame motion. The motion vectors are per frame, so a
        /// higher frame rate blurs less, as a real shutter would.
        float intensity{0.5f};

        /// Taps along each pixel's motion vector; clamped to [2, 32] by
        /// the pass. A per-pixel offset turns a low count's banding into
        /// noise.
        int samples{8};

        /// Longest blur, in pixels, a single pixel's motion may produce.
        float max_radius{32.0f};
    };

    /**
     * @brief Whether @p settings make @ref motion_blur_pass draw at all:
     *        enabled, with a positive shutter scale and radius.
     */
    inline bool motion_blur_active(const motion_blur_settings& settings)
    {
        return settings.enabled && settings.intensity > 0.0f && settings.max_radius > 0.0f;
    }

    /**
     * @brief Runtime-tunable eye adaptation (@ref post_settings::auto_exposure).
     *
     * @ref auto_exposure_pass meters the HDR image tonemap is about to map
     * (the geometric mean of its luminance, reduced on the GPU), converts
     * it to EV100, clamps it to [@ref min_ev, @ref max_ev] and eases a
     * persistent adapted value toward it at @ref speed_up while the scene
     * brightens and @ref speed_down while it darkens. The exposure that
     * maps the adapted brightness to middle grey, raised by
     * @ref compensation stops, replaces @ref post_settings::exposure in
     * @ref tonemap_pass; while this is off the manual exposure applies.
     * The first metered frame after enabling snaps straight to the target
     * rather than easing in from a stale value.
     */
    struct auto_exposure_settings
    {
        bool enabled{false};

        /// Lowest scene brightness, in EV100, the metering follows: a
        /// darker scene is brightened only as far as this.
        float min_ev{-4.0f};

        /// Highest scene brightness, in EV100, the metering follows: a
        /// brighter scene is darkened only as far as this.
        float max_ev{16.0f};

        /// Adaptation rate, per second, of the exponential approach while
        /// the scene gets brighter (0 freezes it).
        float speed_up{3.0f};

        /// Adaptation rate, per second, while the scene gets darker.
        float speed_down{1.0f};

        /// Stops added on top of the adapted exposure (positive brightens).
        float compensation{0.0f};
    };

    /**
     * @brief Runtime-tunable post-processing chain parameters.
     *
     * Lives on @ref renderer (@ref renderer::set_post_settings /
     * @ref renderer::get_post_settings) and is copied into
     * @ref frame_context::post every @ref renderer::render so each post
     * pass can read the fields it owns and rewrite its own UBO only when a
     * value actually changed. @ref exposure and @ref tonemap_op are the
     * exception: @ref renderer::set_post_settings forwards them straight to
     * @ref tonemap_pass::set_exposure / @ref tonemap_pass::set_operator,
     * which already rewrite their UBO immediately and only on change, so
     * @ref tonemap_pass::record has no need to read them back out of the
     * frame context.
     *
     * The scene-wide fog medium is deliberately not part of this struct:
     * it stays on the existing @ref renderer::set_fog / @ref fog_settings
     * path the lit materials apply analytically. @ref volumetric only
     * tunes how @ref volumetric_fog_pass raymarches that same height-fog
     * medium in the post chain.
     *
     * @ref renderer::init seeds it from @c rendering_engine::post_process_settings (the
     * settings.json @c post section, the matching @c ALPHAENGINE_*
     * variables and command-line options), so the engine starts with the
     * persisted values; later changes live only in the renderer.
     */
    struct post_settings
    {
        /// Pre-curve exposure scale @ref tonemap_pass applies before the
        /// operator below, while @ref auto_exposure is off.
        float exposure{1.0f};

        /// Tonemap curve @ref tonemap_pass applies.
        tonemap_operator tonemap_op{tonemap_operator::aces};

        volumetric_fog_settings volumetric{};
        motion_blur_settings motion_blur{};
        bloom_settings bloom{};
        auto_exposure_settings auto_exposure{};
        color_grading_settings grading{};
        taa_settings taa{};
        fxaa_settings fxaa{};
    };
} // namespace rendering_engine
