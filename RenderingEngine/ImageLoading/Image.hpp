#pragma once

#include "Color.hpp"

struct Image
{
	Image(void);
	Image(const Image& image);
	Image(Image&& image);

	Image(const unsigned int width, const unsigned int height, const Color& background);
    Image(const unsigned int width, const unsigned int height, Color* const data);

	virtual ~Image(void);

	const Image& operator=(const Image& image);
	const Image& operator=(Image&& image);

	const unsigned int GetWidth(void) const;
	const unsigned int GetHeight(void) const;
	const Color* const GetPixels(void) const;

	Color GetPixel(const unsigned int x, const unsigned int y) const;
	void SetPixel(const unsigned int x, const unsigned int y, const Color& color);

private:
	Color* m_ImageData;
	unsigned int m_Width, m_Height;
};
