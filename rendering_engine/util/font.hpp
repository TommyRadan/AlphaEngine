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

#include <cstdint>
#include <string>
#include <unordered_map>

#include <core/math/vec2.hpp>
#include <rendering_engine/util/image.hpp>

namespace rendering_engine::util
{
    /**
     * @brief Where one glyph sits in its font's atlas and how it moves the pen.
     *
     * Every length is in pixels at the font's rasterized size, in the UI's
     * y-down convention: the pen sits on the baseline, @ref bearing is the
     * offset from the pen to the top-left corner of the glyph's bitmap (its
     * @c y is negative for ink above the baseline) and @ref advance moves
     * the pen on to the next glyph before kerning. A blank glyph (the space)
     * has a zero @ref size and an empty uv rect but still advances.
     */
    struct glyph_metrics
    {
        float advance{0.0f};
        core::math::vec2 bearing{0.0f, 0.0f};
        core::math::vec2 size{0.0f, 0.0f};
        // The bitmap's rect in the atlas in normalised texture coordinates,
        // (0, 0) being the atlas's top-left texel.
        core::math::vec2 uv_min{0.0f, 0.0f};
        core::math::vec2 uv_max{0.0f, 0.0f};
    };

    /**
     * @brief A TrueType face rasterized at a fixed pixel height into one glyph atlas.
     *
     * Reads the file through the virtual filesystem, parses it with
     * stb_truetype and packs every glyph the face has for printable ASCII
     * (U+0020..U+007E) and the printable Latin-1 supplement
     * (U+00A0..U+00FF) into a single atlas with stb_rect_pack, doubling the
     * atlas until everything fits. Alongside the atlas it records a
     * @ref glyph_metrics per packed codepoint, the kerning of every pair of
     * them (the face's @c kern or GPOS table, read through
     * @c stbtt_GetCodepointKernAdvance) and the face's vertical metrics, so
     * text can be laid out from this object alone: the font file itself is
     * not retained once the constructor returns.
     *
     * The atlas is RGBA8, white with the glyph coverage in alpha, so a
     * texture-times-colour shader tints it to any colour and bilinear
     * filtering never pulls a dark fringe in from the empty texels around a
     * glyph; glyphs are kept a texel apart for the same reason.
     *
     * The constructor throws @c std::runtime_error (after logging) when the
     * file cannot be read, is not a font stb_truetype accepts, or its glyphs
     * do not fit the largest atlas allowed. Copies are deep.
     */
    struct font
    {
        font(const std::string& filename, float font_size);

        /** @brief The glyph packed for @p codepoint, or @c nullptr when the atlas has none. */
        const glyph_metrics* glyph(char32_t codepoint) const;

        /**
         * @brief The kerning adjustment in pixels to add to the pen between
         *        @p left and @p right (usually negative); 0 for a pair the
         *        face does not kern or a codepoint the atlas does not hold.
         */
        float kerning(char32_t left, char32_t right) const;

        /** @brief The pixel height the face was rasterized at: ascent minus descent. */
        float size() const;

        /** @brief Baseline to the top of the tallest ink, in pixels (positive). */
        float ascent() const;

        /** @brief Baseline to the bottom of the lowest ink, in pixels (zero or negative). */
        float descent() const;

        /** @brief Baseline-to-baseline distance between two lines of text, in pixels. */
        float line_height() const;

        /** @brief The packed glyphs: white RGBA8 with the coverage in alpha, row 0 at the top. */
        const image& atlas() const;

    private:
        std::unordered_map<char32_t, glyph_metrics> m_glyphs;
        // Non-zero kerning in pixels, keyed by (left << 32) | right.
        std::unordered_map<std::uint64_t, float> m_kerning;
        image m_atlas;
        float m_size{0.0f};
        float m_ascent{0.0f};
        float m_descent{0.0f};
        float m_line_height{0.0f};
    };
} // namespace rendering_engine::util
