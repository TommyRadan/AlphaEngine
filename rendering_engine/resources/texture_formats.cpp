// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/resources/texture_formats.hpp>

#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    namespace
    {
        bool sampled(const gpu::device& device, gpu::texture_format format)
        {
            return (device.format_support(format) & gpu::texture_usage_sampled) != 0u;
        }
    } // namespace

    gpu::texture_format texture_format_for(assets::texel_format format, assets::color_space space)
    {
        const bool srgb = space == assets::color_space::srgb;
        switch (format)
        {
        case assets::texel_format::rgba8:
            return rgba8_format(space);
        case assets::texel_format::bc1_rgba:
            return srgb ? gpu::texture_format::bc1_rgba_srgb : gpu::texture_format::bc1_rgba_unorm;
        case assets::texel_format::bc3_rgba:
            return srgb ? gpu::texture_format::bc3_rgba_srgb : gpu::texture_format::bc3_rgba_unorm;
        case assets::texel_format::bc4_r:
            return gpu::texture_format::bc4_r_unorm;
        case assets::texel_format::bc5_rg:
            return gpu::texture_format::bc5_rg_unorm;
        case assets::texel_format::bc7_rgba:
            return srgb ? gpu::texture_format::bc7_rgba_srgb : gpu::texture_format::bc7_rgba_unorm;
        case assets::texel_format::astc_4x4:
            return srgb ? gpu::texture_format::astc_4x4_srgb : gpu::texture_format::astc_4x4_unorm;
        }
        return rgba8_format(space);
    }

    assets::compressed_format_support query_compressed_support(const gpu::device& device)
    {
        assets::compressed_format_support support;
        support.bc1 =
            sampled(device, gpu::texture_format::bc1_rgba_unorm) && sampled(device, gpu::texture_format::bc1_rgba_srgb);
        support.bc3 =
            sampled(device, gpu::texture_format::bc3_rgba_unorm) && sampled(device, gpu::texture_format::bc3_rgba_srgb);
        support.bc4 = sampled(device, gpu::texture_format::bc4_r_unorm);
        support.bc5 = sampled(device, gpu::texture_format::bc5_rg_unorm);
        support.bc7 =
            sampled(device, gpu::texture_format::bc7_rgba_unorm) && sampled(device, gpu::texture_format::bc7_rgba_srgb);
        support.astc_4x4 =
            sampled(device, gpu::texture_format::astc_4x4_unorm) && sampled(device, gpu::texture_format::astc_4x4_srgb);
        return support;
    }
} // namespace rendering_engine
