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
        bool mipmaps{false};

        // Per-texture sampler state. The GL backend bakes these via
        // @c glTexParameteri at create time so a texture is fully
        // configured by its descriptor; standalone @ref sampler
        // resources are reserved for future explicit-binding-model
        // backends and currently override per-texture state only on
        // those backends.
        filter_mode min_filter{filter_mode::linear};
        filter_mode mag_filter{filter_mode::linear};
        mipmap_mode mipmap_filter{mipmap_mode::none};
        address_mode address_u{address_mode::repeat};
        address_mode address_v{address_mode::repeat};
        address_mode address_w{address_mode::repeat};
    };

    struct sampler_descriptor
    {
        filter_mode min_filter{filter_mode::linear};
        filter_mode mag_filter{filter_mode::linear};
        mipmap_mode mipmap{mipmap_mode::none};
        address_mode address_u{address_mode::repeat};
        address_mode address_v{address_mode::repeat};
        address_mode address_w{address_mode::repeat};
    };
} // namespace rendering_engine::gpu
