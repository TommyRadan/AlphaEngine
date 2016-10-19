#pragma once

#include "Color.hpp"
#include <GL/Util/ByteBuffer.hpp>

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

	Image(unsigned int width, unsigned int height, const Color& background);
	Image(unsigned int width, unsigned int height, unsigned char* pixels);
	Image(const std::string& filename);

	~Image(void);

	void Load(const std::string& filename);
	void Save(const std::string& filename, ImageFileFormat format);

	unsigned int GetWidth(void) const;
	unsigned int GetHeight(void) const;
	const Color* GetPixels(void) const;

	Color GetPixel(unsigned int x, unsigned int y) const;
	void SetPixel(unsigned int x, unsigned int y, const Color& color);

private:
	Color* image;
	unsigned int width, height;

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
