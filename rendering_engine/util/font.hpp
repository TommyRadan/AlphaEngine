/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rendering_engine/util/image.hpp>
#include <stb_truetype.hpp>

namespace rendering_engine::util
{
    /**
     * @brief A TrueType face rasterized at a fixed pixel height.
     *
     * Reads the whole file into memory, parses it with stb_truetype and
     * pre-rasterizes the printable ASCII range (codepoints 32..127) into RGBA
     * @ref image bitmaps. The constructor throws @c std::runtime_error (after
     * logging) when the file cannot be read or is not a font stb_truetype
     * accepts. Non-copyable: it owns the glyph bitmaps.
     */
    struct font
    {
        font(const std::string& filename, float font_size);

        /**
         * @brief Looks up the pre-rasterized bitmap for @p codepoint.
         *
         * Writes the glyph's bitmap box (pixels, relative to the baseline
         * origin) to @p x0 .. @p y1 and returns the bitmap. For a codepoint no
         * bitmap was rasterized for (anything outside 32..127) it writes an
         * empty box and returns @c nullptr; the lookup never inserts.
         */
        const image* get_image(char32_t codepoint, int* x0, int* y0, int* x1, int* y1) const;

    private:
        std::map<char32_t, std::unique_ptr<image>> m_images;
        std::vector<unsigned char> m_buffer;
        stbtt_fontinfo m_font{};
        float m_scale{0.0f};
    };
} // namespace rendering_engine::util
