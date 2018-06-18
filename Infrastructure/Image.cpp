#include "Image.hpp"

#include <Infrastructure/Exception.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include "StbImage.hpp"
#include <Infrastructure/Log.hpp>

Infrastructure::Image::Image() :
        m_ImageData { nullptr },
        m_Width { 0u },
        m_Height { 0u }
{}

Infrastructure::Image::Image(const std::string& filename) :
        m_ImageData { nullptr },
        m_Width { 0u },
        m_Height { 0u }
{
    int numComponents;
    m_ImageData = (Color*) stbi_load(filename.c_str(), (int*)&m_Width, (int*)&m_Height, &numComponents, 4);

    if (m_ImageData == nullptr)
    {
        LOG_ERROR("Could not load image (%s)", filename.c_str());
        return;
    }
}

Infrastructure::Image::Image(const Image& image) :
        m_Width { image.m_Width },
        m_Height { image.m_Height },
        m_ImageData { new Color[image.m_Width * image.m_Height] }
{
    memcpy(m_ImageData, image.m_ImageData, m_Width * m_Height * sizeof(Color));
}

Infrastructure::Image::Image(Image&& image) noexcept :
        m_Width { image.m_Width },
        m_Height { image.m_Height },
        m_ImageData { image.m_ImageData }
{
    image.m_Width = 0u;
    image.m_Height = 0u;
    image.m_ImageData = nullptr;
}

Infrastructure::Image::Image(uint32_t width, uint32_t height, const Color& background) :
        m_ImageData { new Color[width * height] },
        m_Width { width },
        m_Height { height }
{
    for (uint32_t i = 0; i < width * height; ++i)
    {
        m_ImageData[i] = background;
    }
}

Infrastructure::Image::Image(uint32_t width, uint32_t height, Color* data) :
        m_ImageData { data },
        m_Width { width },
        m_Height { height }
{}

Infrastructure::Image::~Image()
{
    if (m_ImageData == nullptr) return;
    delete[] m_ImageData;
}

Infrastructure::Image& Infrastructure::Image::operator=(const Infrastructure::Image& image)
{
    m_Width = image.m_Width;
    m_Height = image.m_Height;
    memcpy(m_ImageData, image.m_ImageData, m_Width * m_Height * sizeof(Color));
    return *this;
}

Infrastructure::Image& Infrastructure::Image::operator=(Image&& image) noexcept
{
    m_Width = image.m_Width;
    m_Height = image.m_Height;
    m_ImageData = image.m_ImageData;

    image.m_Width = 0u;
    image.m_Height = 0u;
    image.m_ImageData = nullptr;
    return *this;
}

const unsigned int Infrastructure::Image::GetWidth() const
{
    return m_Width;
}

const unsigned int Infrastructure::Image::GetHeight() const
{
    return m_Height;
}

const Infrastructure::Color* const Infrastructure::Image::GetPixels() const
{
    return m_ImageData;
}

Infrastructure::Color Infrastructure::Image::GetPixel(uint32_t x, uint32_t y) const
{
    if (x >= m_Width || y >= m_Height) return Color();
    return m_ImageData[x + y * m_Width];
}

void Infrastructure::Image::SetPixel(uint32_t x, uint32_t y, const Color& color)
{
    if (x >= m_Width || y >= m_Height) return;
    m_ImageData[x + y * m_Width] = color;
}
