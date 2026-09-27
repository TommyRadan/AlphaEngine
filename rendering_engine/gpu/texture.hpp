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
 * @brief @c gpu::texture and @c gpu::sampler descriptors, plus the region
 *        types the upload / copy / readback entry points take.
 */

#pragma once

#include <cstddef>
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
        // for 2D, array and cube textures.
        uint32_t depth{1};

        // Layer count for @c texture_dimension::d2_array. Ignored for
        // the other dimensions: a 2D or 3D texture has one layer and a
        // cube always has six.
        uint32_t array_layers{1};

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

        // Explicit level count, for a texture that wants some but not
        // every level (a bloom pyramid, a prefiltered cube whose
        // roughness steps stop early). 0 leaves the count to
        // @ref mipmaps: the full chain when it is set, one level when
        // it is not. A count past the full chain is clamped. See
        // @ref effective_mip_level_count.
        uint32_t mip_level_count{0};

        // Samples per texel. 1 is an ordinary texture; 2, 4, 8 ... is a
        // multisampled attachment (@c sampler2DMS in a shader). Only
        // valid for 2D textures used as render attachments, single-mip,
        // and never uploaded to; @c device_limits reports the counts
        // the device supports.
        uint32_t sample_count{1};

        // What the texture may be bound as (see @ref texture_usage).
        // Explicit-binding backends (Vulkan) bake it into the image, so
        // a texture attached to a render target needs
        // @c texture_usage_render_attachment and one bound as a
        // storage image (the IBL compute convolution) needs
        // @c texture_usage_storage; the OpenGL backend allows every
        // use unconditionally. The default covers a sampled, uploaded
        // asset.
        texture_usage usage{texture_usage_default};

        // Per-texture sampler state. Every backend bakes these onto the
        // texture object at create time so a texture is fully
        // configured by its descriptor and sampled correctly when its
        // binding carries no separate @ref sampler. A standalone
        // sampler bound to the same binding number (@c binding_kind::sampler)
        // overrides this state for that draw on both backends.
        filter_mode min_filter{filter_mode::linear};
        filter_mode mag_filter{filter_mode::linear};
        mipmap_mode mipmap_filter{mipmap_mode::none};
        address_mode address_u{address_mode::repeat};
        address_mode address_v{address_mode::repeat};
        address_mode address_w{address_mode::repeat};
    };

    // Level count of a full mip chain from the base extent down to a
    // single texel: floor(log2(max extent)) + 1, the chain both
    // backends allocate for @c mipmaps and @c generate_mipmaps fills.
    constexpr uint32_t full_mip_chain_length(uint32_t width, uint32_t height, uint32_t depth = 1)
    {
        uint32_t extent = width > height ? width : height;
        extent = extent > depth ? extent : depth;
        uint32_t levels = 1;
        while (extent > 1)
        {
            extent >>= 1;
            ++levels;
        }
        return levels;
    }

    // The number of mip levels a backend allocates for @p descriptor,
    // the one rule both follow:
    //
    //   - a multisampled texture has exactly one level;
    //   - @c mip_level_count 0 defers to @c mipmaps: the full chain of
    //     the base extent when set, one level otherwise;
    //   - an explicit count is honoured, clamped to the full chain so a
    //     request past the last 1x1 level never over-allocates.
    constexpr uint32_t effective_mip_level_count(const texture_descriptor& descriptor)
    {
        if (descriptor.sample_count > 1)
        {
            return 1;
        }
        const uint32_t depth = descriptor.dimension == texture_dimension::d3 ? descriptor.depth : 1u;
        const uint32_t full = full_mip_chain_length(descriptor.width, descriptor.height, depth);
        if (descriptor.mip_level_count == 0)
        {
            return descriptor.mipmaps ? full : 1u;
        }
        return descriptor.mip_level_count < full ? descriptor.mip_level_count : full;
    }

    // The layer count of a texture of @p dimension: six faces for a
    // cube, @p array_layers for an array, one otherwise.
    constexpr uint32_t effective_array_layers(texture_dimension dimension, uint32_t array_layers)
    {
        switch (dimension)
        {
        case texture_dimension::cube:
            return 6;
        case texture_dimension::d2_array:
            return array_layers == 0 ? 1u : array_layers;
        case texture_dimension::d2:
        case texture_dimension::d3:
            break;
        }
        return 1;
    }

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

    // "No upper LOD clamp": the sampler may read down to the last level
    // of any chain. Matches Vulkan's @c VK_LOD_CLAMP_NONE and GL's
    // default @c GL_TEXTURE_MAX_LOD.
    constexpr float lod_clamp_none = 1000.0f;

    struct sampler_descriptor
    {
        filter_mode min_filter{filter_mode::linear};
        filter_mode mag_filter{filter_mode::linear};
        mipmap_mode mipmap{mipmap_mode::none};
        address_mode address_u{address_mode::repeat};
        address_mode address_v{address_mode::repeat};
        address_mode address_w{address_mode::repeat};

        // Anisotropic filtering: the maximum number of samples along
        // the major axis of a texel footprint. 1 disables it. Clamped
        // to @c device_limits::max_anisotropy, and silently 1 on a
        // device without @c device_features::sampler_anisotropy
        // (GL: @c GL_TEXTURE_MAX_ANISOTROPY, core in 4.6; Vulkan:
        // @c anisotropyEnable / @c maxAnisotropy).
        float max_anisotropy{1.0f};

        // Level-of-detail range and bias. The chain is sampled between
        // @ref lod_min_clamp and @ref lod_max_clamp (in levels, from
        // the base); @ref lod_bias shifts the computed level. The
        // default clamps nothing.
        float lod_min_clamp{0.0f};
        float lod_max_clamp{lod_clamp_none};
        float lod_bias{0.0f};

        // Colour of the border a @c clamp_border lookup lands in.
        border_color border{border_color::opaque_black};

        // Depth-comparison sampling for shadow lookups. When enabled the
        // fetched depth texel is compared against the shader's reference
        // coordinate with @ref compare and the (filtered) 0 / 1 result is
        // returned instead of the depth value — the @c sampler2DShadow /
        // @c samplerCubeShadow contract. Maps to
        // @c GL_TEXTURE_COMPARE_MODE / @c GL_TEXTURE_COMPARE_FUNC on the
        // OpenGL sampler object and to @c compareEnable / @c compareOp on
        // the Vulkan sampler. On Vulkan a standalone sampler reaches the
        // shader through the combined-image-sampler of the texture bound
        // at the same binding number in the same bind group, so the two
        // entries belong together.
        bool compare_enabled{false};
        compare_function compare{compare_function::less_equal};
    };

    // A sub-rectangle of one mip level and one layer of a 2D or 2D-array
    // texture, for @ref device::write_texture_region. Coordinates and
    // extents are in texels of that level: level @c n measures
    // @c max(1, width >> n) by @c max(1, height >> n). @c layer is the
    // array layer (or cube face); 0 for a plain 2D texture.
    struct texture_write_region
    {
        uint32_t mip_level{0};
        uint32_t layer{0};
        uint32_t x{0};
        uint32_t y{0};
        uint32_t width{0};
        uint32_t height{0};
    };

    // A box of one mip level of a texture, for the encoder's
    // buffer <-> texture copies and @ref device::read_texture. @c layer
    // selects the array layer or cube face (0 for 2D / 3D); @c z and
    // @c depth address the slices of a 3D texture and are 0 / 1
    // otherwise. The buffer side is tightly packed rows of
    // @c texel_size_bytes(format) texels, @c width by @c height by
    // @c depth, so it measures @ref texture_region_bytes.
    struct texture_copy_region
    {
        uint32_t mip_level{0};
        uint32_t layer{0};
        uint32_t x{0};
        uint32_t y{0};
        uint32_t z{0};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t depth{1};
    };

    // Bytes a tightly packed buffer needs to hold @p region of a
    // texture of @p format (see @ref texel_size_bytes).
    constexpr size_t texture_region_bytes(texture_format format, const texture_copy_region& region)
    {
        return static_cast<size_t>(region.width) * region.height * region.depth * texel_size_bytes(format);
    }
} // namespace rendering_engine::gpu
