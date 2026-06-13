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
 * @file texture_asset.hpp
 * @brief A GPU texture owned through a shared, reference-counted asset handle.
 */

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    /**
     * @brief A GPU texture decoded from an image file and uploaded once.
     *
     * Produced by @ref asset_cache::load_texture and handed out as a
     * @c std::shared_ptr. The destructor releases the underlying
     * @c gpu::texture, so the GPU resource lives exactly as long as the last
     * live handle — this is the RAII the bare @c gpu::texture handle does not
     * have on its own. The cache itself keeps only a @c std::weak_ptr, so an
     * asset is freed as soon as every consumer drops it.
     *
     * Non-copyable and non-movable: the GPU handle has a single owner (this
     * object) and is freed exactly once in the destructor.
     */
    struct texture_asset
    {
        texture_asset() = default;
        ~texture_asset();

        texture_asset(const texture_asset&) = delete;
        texture_asset& operator=(const texture_asset&) = delete;
        texture_asset(texture_asset&&) = delete;
        texture_asset& operator=(texture_asset&&) = delete;

        // The owned GPU texture. Valid once the cache has uploaded the image.
        gpu::texture texture{};

        // Dimensions of the source image, in texels.
        uint32_t width{0};
        uint32_t height{0};
    };
} // namespace rendering_engine
