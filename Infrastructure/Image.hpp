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

#include <Infrastructure/Color.hpp>
#include <string>

namespace Infrastructure
{
    struct Image
    {
        Image();
        explicit Image(const std::string& filename);
        Image(const Image& image);
        Image(Image&& image) noexcept;

        Image(uint32_t width, uint32_t height, const Color& background);
        Image(uint32_t width, uint32_t height, Color* data);

        virtual ~Image();

        Image& operator=(const Image& image);
        Image& operator=(Image&& image) noexcept;

        const uint32_t GetWidth() const;
        const uint32_t GetHeight() const;
        const Color* const GetPixels() const;

        Color GetPixel(uint32_t x, uint32_t y) const;
        void SetPixel(uint32_t x, uint32_t y, const Color& color);

    private:
        Color* m_ImageData;
        uint32_t m_Width, m_Height;
    };
}
