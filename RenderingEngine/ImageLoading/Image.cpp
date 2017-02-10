#include "Image.hpp"

#include <cstring>

Image::Image(void) :
	m_ImageData{ nullptr },
	m_Width{ 0u },
	m_Height{ 0u }
{}

Image::Image(const Image& image) :
	m_Width { image.m_Width },
	m_Height { image.m_Height },
	m_ImageData { new Color[image.m_Width * image.m_Height] }
{
	memcpy(m_ImageData, image.m_ImageData, m_Width * m_Height * sizeof(Color));
}

Image::Image(Image&& image) :
	m_Width { image.m_Width },
	m_Height { image.m_Height },
	m_ImageData { image.m_ImageData }
{
	image.m_Width = 0u;
	image.m_Height = 0u;
	image.m_ImageData = nullptr;
}

Image::Image(const unsigned int width, const unsigned int height, const Color& background) :
	m_ImageData { new Color[width * height] },
	m_Width { width },
	m_Height { height }
{
	for (unsigned int i = 0; i < width * height; ++i) {
		m_ImageData[i] = background;
	}
}

Image::Image(const unsigned int width, const unsigned int height, Color* const data) :
    m_ImageData { data },
    m_Width { width },
    m_Height { height }
{}

Image::~Image(void)
{
	if (m_ImageData == nullptr) return;
	delete[] m_ImageData;
}

const Image &Image::operator=(const Image& image)
{
	m_Width = image.m_Width;
	m_Height = image.m_Height;
	memcpy(m_ImageData, image.m_ImageData, m_Width * m_Height * sizeof(Color));
	return *this;
}

const Image &Image::operator=(Image&& image)
{
	m_Width = image.m_Width;
	m_Height = image.m_Height;
	m_ImageData = image.m_ImageData;

	image.m_Width = 0u;
	image.m_Height = 0u;
	image.m_ImageData = nullptr;
	return *this;
}

const unsigned int Image::GetWidth(void) const
{
	return m_Width;
}

const unsigned int Image::GetHeight(void) const
{
	return m_Height;
}

const Color* const Image::GetPixels(void) const
{
	return m_ImageData;
}

Color Image::GetPixel(const unsigned int x, const unsigned int y) const
{
	if (x >= m_Width || y >= m_Height) return Color();
	return m_ImageData[x + y * m_Width];
}

void Image::SetPixel(const unsigned int x, const unsigned int y, const Color& color)
{
	if (x >= m_Width || y >= m_Height) return;
	m_ImageData[x + y * m_Width] = color;
}
