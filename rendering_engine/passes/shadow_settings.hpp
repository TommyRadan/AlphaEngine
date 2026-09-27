// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file shadow_settings.hpp
 * @brief Shadow-map configuration, read once during rendering-engine init.
 */

#pragma once

namespace rendering_engine
{
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
} // namespace rendering_engine
