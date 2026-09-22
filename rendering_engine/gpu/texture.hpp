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

/**
 * @file texture.hpp
 * @brief @c gpu::texture and @c gpu::sampler descriptors.
 */

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    struct texture_descriptor
    {
        texture_dimension dimension{texture_dimension::d2};
        texture_format format{texture_format::rgba8_unorm};
        uint32_t width{0};
        uint32_t height{0};

        // Volume slice count for @c texture_dimension::d3. Ignored
        // for 2D and cube textures.
        uint32_t depth{1};

        // When true, the backend allocates a full mip chain and the
        // caller may invoke @c device::generate_mipmaps after
        // uploading the level-0 data. @c false leaves the texture
        // single-mip.
        //
        // The chain is only worth allocating if it is sampled, so the
        // backends derive the minification filter from this flag when
        // @ref mipmap_filter is left at its default: see
        // @ref effective_mipmap_filter.
        bool mipmaps{false};

        // When true, the texture may be bound as a storage image (a
        // shader-writable @c storage_texture binding, e.g. the IBL
        // compute convolution). Explicit-binding backends (Vulkan)
        // add storage usage to the underlying image; the OpenGL
        // backend allows image binding unconditionally and ignores
        // this flag.
        bool storage{false};

        // Per-texture sampler state. Every backend bakes these onto the
        // texture object at create time so a texture is fully
        // configured by its descriptor and sampled correctly when its
        // binding carries no separate @ref sampler. A standalone
        // sampler bound to the same unit (@c binding_kind::sampler)
        // overrides this state for that draw on both backends.
        filter_mode min_filter{filter_mode::linear};
        filter_mode mag_filter{filter_mode::linear};
        mipmap_mode mipmap_filter{mipmap_mode::none};
        address_mode address_u{address_mode::repeat};
        address_mode address_v{address_mode::repeat};
        address_mode address_w{address_mode::repeat};
    };

    // The mip filter a backend applies for a texture created with
    // @p mipmaps and the requested @p filter. This is the one rule both
    // backends follow so a descriptor samples the same way everywhere:
    //
    //   - @p mipmaps false: the texture has a single level, so the
    //     chain filter is @c none whatever was requested (a mip filter
    //     over a one-level texture is either incomplete or pointless).
    //   - @p mipmaps true and @p filter @c none: the caller asked for a
    //     chain but did not say how to sample it, so it samples
    //     trilinearly (@c linear). Pinning level 0 would generate the
    //     whole chain and never read it: aliasing on minified surfaces
    //     and wasted memory.
    //   - @p mipmaps true with an explicit @c nearest / @c linear: as
    //     requested.
    constexpr mipmap_mode effective_mipmap_filter(bool mipmaps, mipmap_mode filter)
    {
        if (!mipmaps)
        {
            return mipmap_mode::none;
        }
        return filter == mipmap_mode::none ? mipmap_mode::linear : filter;
    }

    struct sampler_descriptor
    {
        filter_mode min_filter{filter_mode::linear};
        filter_mode mag_filter{filter_mode::linear};
        mipmap_mode mipmap{mipmap_mode::none};
        address_mode address_u{address_mode::repeat};
        address_mode address_v{address_mode::repeat};
        address_mode address_w{address_mode::repeat};

        // Depth-comparison sampling for shadow lookups. When enabled the
        // fetched depth texel is compared against the shader's reference
        // coordinate with @ref compare and the (filtered) 0 / 1 result is
        // returned instead of the depth value — the @c sampler2DShadow /
        // @c samplerCubeShadow contract. Maps to
        // @c GL_TEXTURE_COMPARE_MODE / @c GL_TEXTURE_COMPARE_FUNC on the
        // OpenGL sampler object and to @c compareEnable / @c compareOp on
        // the Vulkan sampler.
        bool compare_enabled{false};
        compare_function compare{compare_function::less_equal};
    };

    // A sub-rectangle of one mip level of a 2D texture, for
    // @ref device::write_texture_region. Coordinates and extents are in
    // texels of that level: level @c n measures
    // @c max(1, width >> n) by @c max(1, height >> n).
    struct texture_write_region
    {
        uint32_t mip_level{0};
        uint32_t x{0};
        uint32_t y{0};
        uint32_t width{0};
        uint32_t height{0};
    };
} // namespace rendering_engine::gpu
