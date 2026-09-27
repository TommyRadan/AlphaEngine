// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/graphics_settings.hpp>

namespace rendering_engine
{
    const char* graphics_backend_name(graphics_backend backend) noexcept
    {
        switch (backend)
        {
        case graphics_backend::vulkan:
            return "vulkan";
        }
        return "unknown";
    }
} // namespace rendering_engine
