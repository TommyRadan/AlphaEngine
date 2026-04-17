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

#include <rendering_engine/util/color.hpp>
#include <string>

namespace rendering_engine::util
{
    struct image
    {
        image();
        explicit image(const std::string& filename);
        image(const image& image);
        image(image&& image) noexcept;

        image(uint32_t width, uint32_t height, const color& background);
        image(uint32_t width, uint32_t height, color* data);

        virtual ~image();

        image& operator=(const image& image);
        image& operator=(image&& image) noexcept;

        const uint32_t get_width() const;
        const uint32_t get_height() const;
        const color* const get_pixels() const;

        color get_pixel(uint32_t x, uint32_t y) const;
        void set_pixel(uint32_t x, uint32_t y, const color& color);

    private:
        color* m_image_data;
        uint32_t m_width, m_height;
    };
} // namespace rendering_engine::util
