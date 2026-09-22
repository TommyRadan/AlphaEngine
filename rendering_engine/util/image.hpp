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

#include <cstdint>
#include <memory>
#include <string>

namespace rendering_engine::util
{
    /**
     * @brief An RGBA8 pixel buffer, decoded from an image file or built in memory.
     *
     * The pixels always live in a single @c new[] allocation the image owns:
     * the file loader copies stb_image's result into one and releases the
     * decoder's buffer, so every image is destroyed the same way whatever the
     * source of its pixels. Copies are deep. A moved-from image is empty
     * (0x0 with no pixels). Assignment is copy-and-swap, so assigning a larger
     * image over a smaller one, assigning over an empty image, and
     * self-assignment are all safe.
     */
    struct image
    {
        image() = default;

        /** @brief Decodes @p filename as RGBA8; throws @c std::runtime_error (after logging) on failure. */
        explicit image(const std::string& filename);

        image(const image& other);
        image(image&& other) noexcept;

        image(uint32_t width, uint32_t height, const color& background);

        /** @brief Adopts @p data, a @c new color[width * height] allocation the image will @c delete[]. */
        image(uint32_t width, uint32_t height, color* data);

        ~image() = default;

        /** @brief Copy-and-swap assignment; serves as both copy and move assignment. */
        image& operator=(image other) noexcept;

        void swap(image& other) noexcept;

        uint32_t get_width() const;
        uint32_t get_height() const;

        /** @brief The tightly packed RGBA8 pixels, row-major; @c nullptr for an empty image. */
        const color* get_pixels() const;

        /** @brief The pixel at (@p x, @p y), or a zero color when out of range. */
        color get_pixel(uint32_t x, uint32_t y) const;

        /** @brief Sets the pixel at (@p x, @p y); ignored when out of range. */
        void set_pixel(uint32_t x, uint32_t y, const color& color);

    private:
        std::unique_ptr<color[]> m_image_data;
        uint32_t m_width{0u};
        uint32_t m_height{0u};
    };

    inline void swap(image& lhs, image& rhs) noexcept
    {
        lhs.swap(rhs);
    }
} // namespace rendering_engine::util
