#pragma once

#include "Color.hpp"
#include "ByteBuffer.hpp"
#include "Exception.hpp"

#include <string>
#include <vector>
#include <exception>


enum class ImageFileFormat
{
	BMP, TGA, JPEG, PNG
};

struct Image
{
	Image(void);

	Image(const unsigned int width, const unsigned int height, const Color& background);
	Image(const unsigned int width, const unsigned int height, const unsigned char* const pixels);
	Image(const std::string& filename);

	~Image(void);

	void Load(const std::string& filename);
	void Save(const std::string& filename, ImageFileFormat format);

	const unsigned int GetWidth(void) const;
	const unsigned int GetHeight(void) const;
	const Color* GetPixels(void) const;

	Color GetPixel(unsigned int x, unsigned int y) const;
	void SetPixel(unsigned int x, unsigned int y, const Color& color);

private:
	Color* m_ImageData;
	unsigned int m_Width, m_Height;

	Image(const Image&);
	const Image& operator=(const Image&);
		
	void LoadBMP( ByteReader& data );
	void SaveBMP( const std::string& filename );

	void LoadTGA( ByteReader& data );
	void DecodeRLE( ByteReader& data, unsigned int decodedLength, unsigned char bytesPerPixel );
	void SaveTGA( const std::string& filename );
	void EncodeRLE( ByteWriter& data, std::vector<unsigned char>& pixels, unsigned int width );

	void LoadJPEG( ByteReader& data );
	void SaveJPEG( const std::string& filename );

	void LoadPNG( ByteReader& data );
	void SavePNG( const std::string& filename );
};
