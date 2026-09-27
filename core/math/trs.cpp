// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/trs.hpp>

namespace core::math
{
    trs lerp(const trs& a, const trs& b, float t) noexcept
    {
        trs result;
        result.translation = lerp(a.translation, b.translation, t);
        result.rotation = slerp(a.rotation, b.rotation, t);
        result.scale = lerp(a.scale, b.scale, t);
        return result;
    }

    mat4 to_mat4(const trs& pose) noexcept
    {
        return translate(pose.translation) * to_mat4(pose.rotation) * scale(pose.scale);
    }
} // namespace core::math
