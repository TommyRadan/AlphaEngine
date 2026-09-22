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

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"
#include <core/log.hpp>

namespace
{
    std::size_t pixel_count(uint32_t width, uint32_t height)
    {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
} // namespace

rendering_engine::util::image::image(const std::string& filename)
{
    int width = 0;
    int height = 0;
    int num_components = 0;
    stbi_uc* decoded = stbi_load(filename.c_str(), &width, &height, &num_components, 4);

    if (decoded == nullptr)
    {
        LOG_ERR("Could not load image (%s): %s", filename.c_str(), stbi_failure_reason());
        throw std::runtime_error{"Could not load image (" + filename + ")"};
    }

    // stb_image hands back a malloc'd buffer that must go back through
    // stbi_image_free. Copy it into the image's own new[] allocation so every
    // image owns its pixels the same way, then release the decoder's buffer.
    m_width = static_cast<uint32_t>(width);
    m_height = static_cast<uint32_t>(height);
    const std::size_t count = pixel_count(m_width, m_height);
    m_image_data.reset(new color[count]);
    std::memcpy(m_image_data.get(), decoded, count * sizeof(color));
    stbi_image_free(decoded);

    LOG_INF("Loaded image (%s)", filename.c_str());
}

rendering_engine::util::image::image(const image& other) : m_width{other.m_width}, m_height{other.m_height}
{
    if (other.m_image_data == nullptr)
    {
        return;
    }
    const std::size_t count = pixel_count(m_width, m_height);
    m_image_data.reset(new color[count]);
    std::copy_n(other.m_image_data.get(), count, m_image_data.get());
}

// Start empty and swap so the moved-from image is left genuinely empty (0x0,
// no pixels): a defaulted move would null its buffer but keep its dimensions,
// and get_pixel on that state would dereference null.
rendering_engine::util::image::image(image&& other) noexcept : image()
{
    swap(other);
}

rendering_engine::util::image::image(uint32_t width, uint32_t height, const color& background)
    : m_image_data{new color[pixel_count(width, height)]}, m_width{width}, m_height{height}
{
    std::fill_n(m_image_data.get(), pixel_count(width, height), background);
}

rendering_engine::util::image::image(uint32_t width, uint32_t height, color* data)
    : m_image_data{data}, m_width{width}, m_height{height}
{
}

rendering_engine::util::image& rendering_engine::util::image::operator=(image other) noexcept
{
    swap(other);
    return *this;
}

void rendering_engine::util::image::swap(image& other) noexcept
{
    std::swap(m_image_data, other.m_image_data);
    std::swap(m_width, other.m_width);
    std::swap(m_height, other.m_height);
}

uint32_t rendering_engine::util::image::get_width() const
{
    return m_width;
}

uint32_t rendering_engine::util::image::get_height() const
{
    return m_height;
}

const rendering_engine::util::color* rendering_engine::util::image::get_pixels() const
{
    return m_image_data.get();
}

rendering_engine::util::color rendering_engine::util::image::get_pixel(uint32_t x, uint32_t y) const
{
    if (x >= m_width || y >= m_height)
    {
        return color{};
    }
    return m_image_data[x + y * m_width];
}

void rendering_engine::util::image::set_pixel(uint32_t x, uint32_t y, const color& color)
{
    if (x >= m_width || y >= m_height)
    {
        return;
    }
    m_image_data[x + y * m_width] = color;
}
