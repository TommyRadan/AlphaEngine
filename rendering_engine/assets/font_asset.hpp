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
 * @file font_asset.hpp
 * @brief A rasterizable font owned through a reference-counted asset handle.
 */

#pragma once

#include <string>

#include <rendering_engine/util/font.hpp>

namespace rendering_engine
{
    /**
     * @brief A @c util::font loaded from a TTF file at a fixed pixel size.
     *
     * Produced by @ref asset_cache::load_font and handed out as a
     * @c std::shared_ptr, keyed on @c (path, size) so two callers asking for
     * the same face at the same size share one parsed font and its glyph cache.
     * Unlike the texture and mesh assets it owns no GPU resource — the wrapped
     * @ref util::font holds only CPU-side glyph bitmaps — so the default
     * destructor suffices.
     *
     * Non-copyable: @ref util::font owns unique glyph allocations.
     */
    struct font_asset
    {
        font_asset(const std::string& filename, float size) : font{filename, size} {}

        font_asset(const font_asset&) = delete;
        font_asset& operator=(const font_asset&) = delete;

        util::font font;
    };
} // namespace rendering_engine
