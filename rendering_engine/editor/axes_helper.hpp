// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/editor/line_helper.hpp>

namespace rendering_engine::editor
{
    // The three world axes drawn from the origin.
    // +X is red, +Y green, +Z blue. Reposition or
    // orient it through the inherited @ref transform.
    struct axes_helper : public line_helper
    {
        // @p size is the length of each axis line. Geometry is baked once
        // at construction.
        explicit axes_helper(float size = 1.0f);
    };
} // namespace rendering_engine::editor
