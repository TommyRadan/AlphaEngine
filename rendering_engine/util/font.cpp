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

#define STB_TRUETYPE_IMPLEMENTATION
#include <core/log.hpp>
#include <rendering_engine/util/font.hpp>

#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <vector>

rendering_engine::util::font::font(const std::string& filename, float font_size)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        LOG_ERR("Could not open font (%s)", filename.c_str());
        throw std::runtime_error{"Could not open font (" + filename + ")"};
    }

    // tellg() reports -1 on failure; a zero-length file is no font either.
    const std::streamoff size = file.tellg();
    if (size <= 0)
    {
        LOG_ERR("Could not read font (%s): empty file or unknown size", filename.c_str());
        throw std::runtime_error{"Could not read font (" + filename + ")"};
    }

    file.seekg(0, std::ios::beg);
    m_buffer.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(m_buffer.data()), static_cast<std::streamsize>(size)))
    {
        LOG_ERR("Could not read font (%s)", filename.c_str());
        throw std::runtime_error{"Could not read font (" + filename + ")"};
    }

    const int offset = stbtt_GetFontOffsetForIndex(m_buffer.data(), 0);
    if (offset < 0 || stbtt_InitFont(&m_font, m_buffer.data(), offset) == 0)
    {
        LOG_ERR("Could not parse font (%s): not a TrueType/OpenType font", filename.c_str());
        throw std::runtime_error{"Could not parse font (" + filename + ")"};
    }
    m_scale = stbtt_ScaleForPixelHeight(&m_font, font_size);

    for (char32_t c = 32; c < 128; ++c)
    {
        int w = 0;
        int h = 0;
        unsigned char* bitmap =
            stbtt_GetCodepointBitmap(&m_font, m_scale, m_scale, static_cast<int>(c), &w, &h, nullptr, nullptr);

        auto img = std::make_unique<image>(static_cast<uint32_t>(w), static_cast<uint32_t>(h), color{0, 0, 0, 0});
        // The rasterizer returns null (with a zero box) for an empty glyph such
        // as the space; anything else is a malloc'd bitmap that must be handed
        // back to stb_truetype once its pixels have been copied out.
        if (bitmap != nullptr)
        {
            for (int j = 0; j < h; ++j)
            {
                for (int i = 0; i < w; ++i)
                {
                    const uint8_t c_value = bitmap[j * w + i];
                    img->set_pixel(
                        static_cast<uint32_t>(i), static_cast<uint32_t>(j), color{c_value, c_value, c_value, c_value});
                }
            }
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        m_images[c] = std::move(img);
    }

    LOG_INF("Loaded font \"%s\"", filename.c_str());
}

const rendering_engine::util::image*
rendering_engine::util::font::get_image(char32_t codepoint, int* x0, int* y0, int* x1, int* y1) const
{
    const auto it = m_images.find(codepoint);
    if (it == m_images.end())
    {
        for (int* out : {x0, y0, x1, y1})
        {
            if (out != nullptr)
            {
                *out = 0;
            }
        }
        return nullptr;
    }

    stbtt_GetCodepointBitmapBoxSubpixel(
        &m_font, static_cast<int>(codepoint), m_scale, m_scale, 0.0f, 0.0f, x0, y0, x1, y1);
    return it->second.get();
}
