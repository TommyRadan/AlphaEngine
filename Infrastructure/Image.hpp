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
