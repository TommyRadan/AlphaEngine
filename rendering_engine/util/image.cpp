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

#include "image.hpp"

#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"
#include <infrastructure/log.hpp>

rendering_engine::util::image::image() : m_image_data{nullptr}, m_width{0u}, m_height{0u} {}

rendering_engine::util::image::image(const std::string& filename) : m_image_data{nullptr}, m_width{0u}, m_height{0u}
{
    int num_components;
    m_image_data = (color*)stbi_load(filename.c_str(), (int*)&m_width, (int*)&m_height, &num_components, 4);

    if (m_image_data == nullptr)
    {
        LOG_ERR("Could not load image (%s)", filename.c_str());
        throw std::runtime_error{"Could not load image (" + filename + ")"};
    }

    LOG_INF("Loaded image (%s)", filename.c_str());
}

rendering_engine::util::image::image(const image& image)
    : m_width{image.m_width}, m_height{image.m_height}, m_image_data{new color[image.m_width * image.m_height]}
{
    memcpy(m_image_data, image.m_image_data, m_width * m_height * sizeof(color));
}

rendering_engine::util::image::image(image&& image) noexcept
    : m_width{image.m_width}, m_height{image.m_height}, m_image_data{image.m_image_data}
{
    image.m_width = 0u;
    image.m_height = 0u;
    image.m_image_data = nullptr;
}

rendering_engine::util::image::image(uint32_t width, uint32_t height, const color& background)
    : m_image_data{new color[width * height]}, m_width{width}, m_height{height}
{
    for (uint32_t i = 0; i < width * height; ++i)
    {
        m_image_data[i] = background;
    }
}

rendering_engine::util::image::image(uint32_t width, uint32_t height, color* data)
    : m_image_data{data}, m_width{width}, m_height{height}
{
}

rendering_engine::util::image::~image()
{
    if (m_image_data == nullptr)
        return;
    delete[] m_image_data;
}

rendering_engine::util::image& rendering_engine::util::image::operator=(const rendering_engine::util::image& image)
{
    m_width = image.m_width;
    m_height = image.m_height;
    memcpy(m_image_data, image.m_image_data, m_width * m_height * sizeof(color));
    return *this;
}

rendering_engine::util::image& rendering_engine::util::image::operator=(image&& image) noexcept
{
    m_width = image.m_width;
    m_height = image.m_height;
    m_image_data = image.m_image_data;

    image.m_width = 0u;
    image.m_height = 0u;
    image.m_image_data = nullptr;
    return *this;
}

const unsigned int rendering_engine::util::image::get_width() const
{
    return m_width;
}

const unsigned int rendering_engine::util::image::get_height() const
{
    return m_height;
}

const rendering_engine::util::color* const rendering_engine::util::image::get_pixels() const
{
    return m_image_data;
}

rendering_engine::util::color rendering_engine::util::image::get_pixel(uint32_t x, uint32_t y) const
{
    if (x >= m_width || y >= m_height)
        return color();
    return m_image_data[x + y * m_width];
}

void rendering_engine::util::image::set_pixel(uint32_t x, uint32_t y, const color& color)
{
    if (x >= m_width || y >= m_height)
        return;
    m_image_data[x + y * m_width] = color;
}
