// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file post_process_settings.hpp
 * @brief Startup values of the post-processing chain and its `settings.json` / `ALPHAENGINE_*` / command-line
 *        surface.
 */

#pragma once

#include <string>

namespace rendering_engine
{
    /** @brief Tonemap curve selected by @ref post_process_settings::tonemap. */
    enum class tonemap_curve
    {
        none,     /**< No curve: the exposed colour is only clamped and gamma encoded. */
        reinhard, /**< Reinhard's x / (1 + x) shoulder. */
        aces,     /**< Krzysztof Narkowicz's ACES filmic approximation. */
    };

    /** @brief The lowercase name of @p curve (`none`, `reinhard`, `aces`). */
    const char* tonemap_curve_name(tonemap_curve curve) noexcept;

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
} // namespace rendering_engine
