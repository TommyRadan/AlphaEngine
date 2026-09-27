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

// stb_rect_pack must precede stb_truetype so the packer stb_truetype
// builds its atlases with is the skyline packer rather than its naive
// fallback. Both implementations live in this translation unit only.
#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#include <stb_rect_pack.hpp>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.hpp>

#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>
#include <rendering_engine/assets/font.hpp>

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    // The codepoint ranges an atlas carries, where the face has glyphs for
    // them: printable ASCII and the printable Latin-1 supplement.
    struct codepoint_range
    {
        int first;
        int last;
    };
    constexpr codepoint_range atlas_ranges[] = {{0x20, 0x7E}, {0xA0, 0xFF}};

    // The atlas is square; it starts at the smallest power of two whose
    // area covers the glyph boxes and doubles until they pack, up to this
    // edge.
    constexpr int min_atlas_extent = 64;
    constexpr int max_atlas_extent = 4096;

    // Empty texels between packed glyphs, so a bilinear sample at a glyph's
    // edge never reads its neighbour.
    constexpr int glyph_padding = 1;

    std::uint64_t kerning_key(char32_t left, char32_t right)
    {
        return (static_cast<std::uint64_t>(left) << 32) | static_cast<std::uint64_t>(right);
    }
} // namespace

rendering_engine::font::font(const std::string& filename, float font_size)
{
    // Negated, so a NaN size is rejected too.
    if (!(font_size > 0.0f))
    {
        LOG_ERR("Could not load font (%s): invalid pixel size %g", filename.c_str(), static_cast<double>(font_size));
        throw std::runtime_error{"Could not load font (" + filename + ")"};
    }

    // The file comes through the virtual filesystem: a relative name is
    // looked up in the mounted asset root.
    std::vector<std::byte> bytes;
    std::string error;
    if (!core::default_vfs().read_file(core::platform::utf8_path(filename), bytes, &error))
    {
        LOG_ERR("Could not open font (%s): %s", filename.c_str(), error.c_str());
        throw std::runtime_error{"Could not open font (" + filename + ")"};
    }

    // A zero-length file is no font.
    if (bytes.empty())
    {
        LOG_ERR("Could not read font (%s): empty file", filename.c_str());
        throw std::runtime_error{"Could not read font (" + filename + ")"};
    }

    // stb_truetype reads the face in place, so it gets its own unsigned
    // copy of the bytes; nothing refers to it once the atlas is built.
    std::vector<unsigned char> buffer(bytes.size());
    std::memcpy(buffer.data(), bytes.data(), bytes.size());

    stbtt_fontinfo info{};
    const int offset = stbtt_GetFontOffsetForIndex(buffer.data(), 0);
    if (offset < 0 || stbtt_InitFont(&info, buffer.data(), offset) == 0)
    {
        LOG_ERR("Could not parse font (%s): not a TrueType/OpenType font", filename.c_str());
        throw std::runtime_error{"Could not parse font (" + filename + ")"};
    }
    const float scale = stbtt_ScaleForPixelHeight(&info, font_size);

    // Only the codepoints the face has a glyph for are packed: a missing
    // one would otherwise be packed as the face's "missing glyph" box, and
    // glyph() reports it as absent instead.
    std::vector<int> codepoints;
    for (const codepoint_range& range : atlas_ranges)
    {
        for (int codepoint = range.first; codepoint <= range.last; ++codepoint)
        {
            if (stbtt_FindGlyphIndex(&info, codepoint) != 0)
            {
                codepoints.push_back(codepoint);
            }
        }
    }
    if (codepoints.empty())
    {
        LOG_ERR("Could not load font (%s): it has no glyph for any printable ASCII or Latin-1 codepoint",
                filename.c_str());
        throw std::runtime_error{"Could not load font (" + filename + ")"};
    }

    // Size the first attempt from the total area of the padded glyph boxes.
    std::size_t glyph_area = 0;
    for (const int codepoint : codepoints)
    {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
        glyph_area +=
            static_cast<std::size_t>(x1 - x0 + glyph_padding) * static_cast<std::size_t>(y1 - y0 + glyph_padding);
    }
    int extent = min_atlas_extent;
    while (extent < max_atlas_extent &&
           static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent) < glyph_area)
    {
        extent *= 2;
    }

    // Pack and rasterize; a pass whose glyphs do not all fit retries at
    // twice the edge. With every codepoint known to the face, a failed
    // pack is the only way stbtt_PackFontRanges reports failure.
    std::vector<stbtt_packedchar> packed(codepoints.size());
    std::vector<unsigned char> coverage;
    bool fitted = false;
    for (;;)
    {
        coverage.assign(static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent), 0);
        stbtt_pack_context context{};
        if (stbtt_PackBegin(&context, coverage.data(), extent, extent, 0, glyph_padding, nullptr) != 0)
        {
            stbtt_PackSetOversampling(&context, 1, 1);

            stbtt_pack_range range{};
            range.font_size = font_size;
            range.array_of_unicode_codepoints = codepoints.data();
            range.num_chars = static_cast<int>(codepoints.size());
            range.chardata_for_range = packed.data();
            fitted = stbtt_PackFontRanges(&context, buffer.data(), 0, &range, 1) != 0;
            stbtt_PackEnd(&context);
        }
        if (fitted || extent >= max_atlas_extent)
        {
            break;
        }
        extent *= 2;
    }
    if (!fitted)
    {
        LOG_ERR("Could not load font (%s): %zu glyphs at %g px do not fit a %dx%d atlas",
                filename.c_str(),
                codepoints.size(),
                static_cast<double>(font_size),
                max_atlas_extent,
                max_atlas_extent);
        throw std::runtime_error{"Could not load font (" + filename + ")"};
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    m_size = font_size;
    m_ascent = static_cast<float>(ascent) * scale;
    m_descent = static_cast<float>(descent) * scale;
    m_line_height = static_cast<float>(ascent - descent + line_gap) * scale;

    const float texel = 1.0f / static_cast<float>(extent);
    for (std::size_t i = 0; i < codepoints.size(); ++i)
    {
        const stbtt_packedchar& source = packed[i];
        glyph_metrics metrics{};
        metrics.advance = source.xadvance;
        metrics.bearing = core::math::vec2{source.xoff, source.yoff};
        metrics.size =
            core::math::vec2{static_cast<float>(source.x1 - source.x0), static_cast<float>(source.y1 - source.y0)};
        metrics.uv_min = core::math::vec2{static_cast<float>(source.x0) * texel, static_cast<float>(source.y0) * texel};
        metrics.uv_max = core::math::vec2{static_cast<float>(source.x1) * texel, static_cast<float>(source.y1) * texel};
        m_glyphs.emplace(static_cast<char32_t>(codepoints[i]), metrics);
    }

    // Every packed pair's kerning, from the face's kern or GPOS table
    // (stb_truetype answers 0 at once for a face with neither). Only the
    // non-zero pairs are kept.
    for (const int left : codepoints)
    {
        for (const int right : codepoints)
        {
            const int advance = stbtt_GetCodepointKernAdvance(&info, left, right);
            if (advance != 0)
            {
                m_kerning.emplace(kerning_key(static_cast<char32_t>(left), static_cast<char32_t>(right)),
                                  static_cast<float>(advance) * scale);
            }
        }
    }

    // White texels carrying the coverage in alpha: see the header.
    m_atlas = image{static_cast<uint32_t>(extent), static_cast<uint32_t>(extent), color{255, 255, 255, 0}};
    for (int y = 0; y < extent; ++y)
    {
        for (int x = 0; x < extent; ++x)
        {
            const unsigned char alpha =
                coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(extent) + static_cast<std::size_t>(x)];
            if (alpha != 0)
            {
                m_atlas.set_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), color{255, 255, 255, alpha});
            }
        }
    }

    LOG_INF("Loaded font \"%s\" at %g px: %zu glyphs in a %dx%d atlas, %zu kerning pairs",
            filename.c_str(),
            static_cast<double>(font_size),
            m_glyphs.size(),
            extent,
            extent,
            m_kerning.size());
}

const rendering_engine::glyph_metrics* rendering_engine::font::glyph(char32_t codepoint) const
{
    const auto it = m_glyphs.find(codepoint);
    return it == m_glyphs.end() ? nullptr : &it->second;
}

float rendering_engine::font::kerning(char32_t left, char32_t right) const
{
    const auto it = m_kerning.find(kerning_key(left, right));
    return it == m_kerning.end() ? 0.0f : it->second;
}

float rendering_engine::font::size() const
{
    return m_size;
}

float rendering_engine::font::ascent() const
{
    return m_ascent;
}

float rendering_engine::font::descent() const
{
    return m_descent;
}

float rendering_engine::font::line_height() const
{
    return m_line_height;
}

const rendering_engine::image& rendering_engine::font::atlas() const
{
    return m_atlas;
}
