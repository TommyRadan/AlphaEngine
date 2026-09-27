// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file texture_formats.hpp
 * @brief Where the CPU asset module's texel descriptions meet the device's
 *        texture formats.
 */

#pragma once

#include <assets/color.hpp>
#include <assets/texture_decode.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    /**
     * @brief The 8-bit RGBA texel format that samples an image authored in
     *        @p space as linear values: @c rgba8_srgb for sRGB content (the
     *        hardware decodes on sample), @c rgba8_unorm for linear data.
     *        The one place the RGBA8 upload sites map a colour space to a
     *        format.
     */
    constexpr gpu::texture_format rgba8_format(assets::color_space space)
    {
        return space == assets::color_space::srgb ? gpu::texture_format::rgba8_srgb : gpu::texture_format::rgba8_unorm;
    }

    /**
     * @brief The device format decoded texels of @p format are uploaded as
     *        when sampled in @p space: the family's sRGB or unorm form,
     *        where it has both (BC4 and BC5 are unorm only).
     */
    gpu::texture_format texture_format_for(assets::texel_format format, assets::color_space space);

    /**
     * @brief The block-compressed families @p device samples (through
     *        @c device::format_support), for the KTX2 decoder to choose
     *        its transcode target from. A colour family counts only when
     *        both its unorm and its sRGB form sample.
     */
    assets::compressed_format_support query_compressed_support(const gpu::device& device);
} // namespace rendering_engine
