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

#include "Image.hpp"

#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"
#include <Infrastructure/log.hpp>

RenderingEngine::Util::Image::Image() :
        m_ImageData { nullptr },
        m_Width { 0u },
        m_Height { 0u }
{}

RenderingEngine::Util::Image::Image(const std::string& filename) :
        m_ImageData { nullptr },
        m_Width { 0u },
        m_Height { 0u }
{
    int numComponents;
    m_ImageData = (Color*) stbi_load(filename.c_str(), (int*)&m_Width, (int*)&m_Height, &numComponents, 4);

    if (m_ImageData == nullptr)
    {
        LOG_ERR("Could not load image (%s)", filename.c_str());
        throw std::runtime_error{"Could not load image (" + filename + ")"};
    }

    LOG_INF("Loaded image (%s)", filename.c_str());
}

RenderingEngine::Util::Image::Image(const Image& image) :
        m_Width { image.m_Width },
        m_Height { image.m_Height },
        m_ImageData { new Color[image.m_Width * image.m_Height] }
{
    memcpy(m_ImageData, image.m_ImageData, m_Width * m_Height * sizeof(Color));
}

RenderingEngine::Util::Image::Image(Image&& image) noexcept :
        m_Width { image.m_Width },
        m_Height { image.m_Height },
        m_ImageData { image.m_ImageData }
{
    image.m_Width = 0u;
    image.m_Height = 0u;
    image.m_ImageData = nullptr;
}

RenderingEngine::Util::Image::Image(uint32_t width, uint32_t height, const Color& background) :
        m_ImageData { new Color[width * height] },
        m_Width { width },
        m_Height { height }
{
    for (uint32_t i = 0; i < width * height; ++i)
    {
        m_ImageData[i] = background;
    }
}

RenderingEngine::Util::Image::Image(uint32_t width, uint32_t height, Color* data) :
        m_ImageData { data },
        m_Width { width },
        m_Height { height }
{}

RenderingEngine::Util::Image::~Image()
{
    if (m_ImageData == nullptr) return;
    delete[] m_ImageData;
}

RenderingEngine::Util::Image& RenderingEngine::Util::Image::operator=(const RenderingEngine::Util::Image& image)
{
    m_Width = image.m_Width;
    m_Height = image.m_Height;
    memcpy(m_ImageData, image.m_ImageData, m_Width * m_Height * sizeof(Color));
    return *this;
}

RenderingEngine::Util::Image& RenderingEngine::Util::Image::operator=(Image&& image) noexcept
{
    m_Width = image.m_Width;
    m_Height = image.m_Height;
    m_ImageData = image.m_ImageData;

    image.m_Width = 0u;
    image.m_Height = 0u;
    image.m_ImageData = nullptr;
    return *this;
}

const unsigned int RenderingEngine::Util::Image::GetWidth() const
{
    return m_Width;
}

const unsigned int RenderingEngine::Util::Image::GetHeight() const
{
    return m_Height;
}

const RenderingEngine::Util::Color* const RenderingEngine::Util::Image::GetPixels() const
{
    return m_ImageData;
}

RenderingEngine::Util::Color RenderingEngine::Util::Image::GetPixel(uint32_t x, uint32_t y) const
{
    if (x >= m_Width || y >= m_Height) return Color();
    return m_ImageData[x + y * m_Width];
}

void RenderingEngine::Util::Image::SetPixel(uint32_t x, uint32_t y, const Color& color)
{
    if (x >= m_Width || y >= m_Height) return;
    m_ImageData[x + y * m_Width] = color;
}
