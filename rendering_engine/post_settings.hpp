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

#pragma once

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
     * Mirrors the fixed values @ref bloom_pass used to bake once at
     * construction: a threshold of 1.0 HDR luminance, a soft knee half the
     * threshold wide, and an overall glow strength of 0.6 spread across the
     * blur pyramid's mips. @ref bloom_pass::record rewrites its UBOs only
     * when a field here differs from what it last uploaded, and a resize
     * (which rebuilds the whole pyramid) rebakes from whatever was last
     * applied rather than these compiled-in defaults.
     */
    struct bloom_settings
    {
        /// Runtime on/off switch. Disabling it does not remove the pass
        /// from the frame graph — @ref bloom_pass::record simply skips its
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
     * pass's projection jitter and is decided once, at @ref context::init,
     * from @c core::settings::graphics.temporal_aa and the drawable size.
     * @ref context::set_post_settings overwrites whatever value it is
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
     * @ref fxaa_pass always stays in the frame graph — it is the pass that
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
     * @c reference_height, set through @ref context::set_fog) as a
     * participating medium lit by the scene lights, so the medium itself
     * is not duplicated here: with a height density of 0 there is nothing
     * to march and the pass draws nothing even while enabled. These fields
     * only tune how it is marched and lit. @ref volumetric_fog_pass::record
     * reads them every frame; there is no baked state to invalidate.
     *
     * Off by default, so the pass stays in the frame graph but records no
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
     * @brief Runtime-tunable post-processing chain parameters.
     *
     * Lives on @ref context (@ref context::set_post_settings /
     * @ref context::get_post_settings) and is copied into
     * @ref frame_context::post every @ref context::render so each post
     * pass can read the fields it owns and rewrite its own UBO only when a
     * value actually changed. @ref exposure and @ref tonemap_op are the
     * exception: @ref context::set_post_settings forwards them straight to
     * @ref tonemap_pass::set_exposure / @ref tonemap_pass::set_operator,
     * which already rewrite their UBO immediately and only on change, so
     * @ref tonemap_pass::record has no need to read them back out of the
     * frame context.
     *
     * The scene-wide fog medium is deliberately not part of this struct:
     * it stays on the existing @ref context::set_fog / @ref fog_settings
     * path the lit materials apply analytically. @ref volumetric only
     * tunes how @ref volumetric_fog_pass raymarches that same height-fog
     * medium in the post chain.
     */
    struct post_settings
    {
        /// Pre-curve exposure scale @ref tonemap_pass applies before the
        /// operator below.
        float exposure{1.0f};

        /// Tonemap curve @ref tonemap_pass applies.
        tonemap_operator tonemap_op{tonemap_operator::aces};

        volumetric_fog_settings volumetric{};
        bloom_settings bloom{};
        taa_settings taa{};
        fxaa_settings fxaa{};
    };
} // namespace rendering_engine
