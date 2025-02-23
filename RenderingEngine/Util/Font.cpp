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
#include <RenderingEngine/Util/Font.hpp>
#include <Infrastructure/Log.hpp>

#include <vector>
#include <fstream>

RenderingEngine::Util::Font::Font(const std::string& filename, float fontSize)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    m_Buffer.resize(size);
    file.read((char *)m_Buffer.data(), size);

    int ascent, baseline;

    stbtt_InitFont(&m_Font, m_Buffer.data(), stbtt_GetFontOffsetForIndex(m_Buffer.data(), 0));
    m_Scale = stbtt_ScaleForPixelHeight(&m_Font, fontSize);
    stbtt_GetFontVMetrics(&m_Font, &ascent, 0, 0);

    for (unsigned char c = 32; c < 128; c++)
    {
        int w, h;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&m_Font, m_Scale, m_Scale, c, &w, &h, 0, 0);

        auto image = new Image(w, h, Color{ 0, 0, 0, 0 });
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                uint8_t cValue = bitmap[j * w + i];
                image->SetPixel(i, j, Color{ cValue, cValue, cValue, cValue });
            }
        }
        m_Images[c] = image;
    }

    LOG_INFO("Loaded font \"%s\"", filename.c_str());
}

const RenderingEngine::Util::Image* RenderingEngine::Util::Font::GetImage(char letter, int *x0, int *y0, int* x1, int* y1)
{
    stbtt_GetCodepointBitmapBoxSubpixel(&m_Font, letter, m_Scale, m_Scale, 0, 0, x0, y0, x1, y1);
    return m_Images[letter];
}
