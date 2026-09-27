// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file camera_settings.hpp
 * @brief Default camera configuration, from `settings.json`'s `camera` section.
 */

#pragma once

namespace rendering_engine
{
    /** @brief Camera configuration. */
    struct camera_settings
    {
        /** @brief Vertical field of view of the perspective camera, in degrees. */
        float field_of_view{70.0f};
    };
} // namespace rendering_engine
