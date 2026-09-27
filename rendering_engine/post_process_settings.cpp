// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/post_process_settings.hpp>

namespace rendering_engine
{
    const char* tonemap_curve_name(tonemap_curve curve) noexcept
    {
        switch (curve)
        {
        case tonemap_curve::none:
            return "none";
        case tonemap_curve::reinhard:
            return "reinhard";
        case tonemap_curve::aces:
            return "aces";
        }
        return "unknown";
    }
} // namespace rendering_engine
