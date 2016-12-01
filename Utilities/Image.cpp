#include "Image.hpp"

#include "libjpeg/jpeglib.h"
#include "libpng/png.h"

#include <fstream>
#include <cstring>
#include <cstdlib>

Image::Image(void) :
	m_ImageData{ nullptr },
	m_Width{ 0u },
	m_Height{ 0u }
{}

Image::Image(const unsigned int width, const unsigned int height, const Color& background )
{
	m_ImageData = new Color[ width * height ];
	for (unsigned int i = 0; i < (unsigned int)( width * height ); i++ )
		m_ImageData[i] = background;

	m_Width = width;
	m_Height = height;
}

Image::Image(const unsigned int width, const unsigned int height, const unsigned char* const pixels)
{
	m_ImageData = new Color[ width * height ];

	for (unsigned int x = 0; x < width; x++ )
	{
		for (unsigned int y = 0; y < height; y++ )
		{
			unsigned int o = ( x + y * width ) * sizeof( Color );
			m_ImageData[ x + y * width ] = Color( pixels[o+0], pixels[o+1], pixels[o+2], pixels[o+3] );
		}
	}

	m_Width = width;
	m_Height = height;
}

Image::Image(const std::string& filename) :
	m_ImageData { nullptr },
	m_Width { 0u },
	m_Height { 0u }
{
	Load(filename);
}

Image::~Image()
{
	if (m_ImageData) delete[] m_ImageData;
}

void Image::Load( const std::string& filename )
{
	// Unload image
	if (m_ImageData) delete[] m_ImageData;
	m_ImageData = nullptr;
	m_Width = 0u;
	m_Height = 0u;

	// Load data from file
	std::ifstream file( filename.c_str(), std::ios::binary | std::ios::ate );
	if (!file.is_open()) throw Exception("Could not load m_ImageData file!");

	unsigned int fileSize = (unsigned int)file.tellg();
	file.seekg( 0, std::ios::beg );

	ByteReader data( fileSize, true );
	file.read( (char*)data.Data(), fileSize );
		
	file.close();

	// Determine format and process
	if ( data.PeekByte( 0 ) == 'B' && data.PeekByte( 1 ) == 'M' )
		LoadBMP( data );
	else if ( data.Compare( data.Length() - 18, 18, (const unsigned char*)"TRUEVISION-XFILE." ) )
		LoadTGA( data );
	else if ( data.PeekByte( 0 ) == 0xFF && data.PeekByte( 1 ) == 0xD8 )
		LoadJPEG( data );
	else if ( data.Compare( 0, 4, (const unsigned char*)"\x89PNG" ) )
		LoadPNG( data );
	else
		throw Exception("Bad m_ImageData format!");
}

void Image::Save( const std::string& filename, ImageFileFormat format )
{
	if ( m_ImageData == 0 || m_Width == 0 || m_Height == 0 ) return;

	if ( format == ImageFileFormat::BMP )
		SaveBMP( filename );
	else if ( format == ImageFileFormat::TGA )
		SaveTGA( filename );
	else if ( format == ImageFileFormat::JPEG )
		SaveJPEG( filename );
	else if ( format == ImageFileFormat::PNG )
		SavePNG( filename );
	else
		throw Exception("Bad m_ImageData format!");
}

const unsigned int Image::GetWidth(void) const
{
	return m_Width;
}

const unsigned int Image::GetHeight(void) const
{
	return m_Height;
}

const Color* Image::GetPixels() const
{
	return m_ImageData;
}

Color Image::GetPixel( unsigned int x, unsigned int y ) const
{
	if ( x >= m_Width || y >= m_Height ) return Color();
	return m_ImageData[ x + y * m_Width ];
}

void Image::SetPixel( unsigned int x, unsigned int y, const Color& color )
{
	if ( x >= m_Width || y >= m_Height ) return;
	m_ImageData[ x + y * m_Width ] = color;
}

void Image::LoadBMP( ByteReader& data )
{
	// BMP header
	data.Advance( 2 + 4 + 4 ); // Skip magic number, file size and application specific data
	unsigned int pixelOffset = data.ReadUint();

	// DIB header
	if ( data.ReadUint() != 40 ) throw Exception("Bad m_ImageData format!"); // Only version 1 is currently supported
	unsigned int width = data.ReadUint();
	int rawHeight = data.ReadInt();
	unsigned int height = abs( rawHeight );
	if ( width == 0 || height == 0 ) throw Exception("Bad m_ImageData format!");
	if ( width > USHRT_MAX || height > USHRT_MAX ) throw Exception("Bad m_ImageData format!");
	if ( data.ReadUshort() != 1 ) throw Exception("Bad m_ImageData format!"); // Color planes
	if ( data.ReadUshort() != 24 ) throw Exception("Bad m_ImageData format!"); // Bits per pixel
	if ( data.ReadUint() != 0 ) throw Exception("Bad m_ImageData format!"); // Compression
	data.Advance( 4 ); // Skip pixel array size (very unreliable, a value of 0 is not uncommon)
	data.Advance( 4 + 4 ); // Skip X/Y resolution
	if ( data.ReadUint() != 0 ) throw Exception("Bad m_ImageData format!"); // Palette colors
	if ( data.ReadUint() != 0 ) throw Exception("Bad m_ImageData format!"); // Important colors

	// Pixel data
	data.Move( pixelOffset );
	unsigned int padding = ( width * 3 ) % 4;

	m_ImageData = new Color[ width * height ];
		
	for (unsigned short y = 0; y < height; y++)
	{
		for (unsigned short x = 0; x < width; x++)
		{
			unsigned int o = rawHeight > 0 ? x + ( height - y - 1 ) * width : x + y * width; // Flip image vertically if height is negative
			m_ImageData[ o ] = Color( data.PeekByte( 2 ), data.PeekByte( 1 ), data.PeekByte( 0 ) ); // BGR byte order
			data.Advance( 3 );
			if ( x == width - 1 ) data.Advance( padding );
		}
	}

	this->m_Width = (unsigned short)width;
	this->m_Height = (unsigned short)height;
}

void Image::SaveBMP( const std::string& filename )
{
	ByteWriter data( true );
	unsigned int padding = ( m_Width * 3 ) % 4;

	// BMP header
	data.WriteUbyte( 'B' );
	data.WriteUbyte( 'M' );
	data.WriteUint( m_Width * m_Height * 3 + padding * m_Height + 54 ); // File size
	data.WriteUint( 0 ); // Two application specific shorts
	data.WriteUint( 54 ); // Offset to pixel array

	// DIB header
	data.WriteUint( 40 ); // Header size
	data.WriteUint( m_Width );
	data.WriteUint( m_Height );
	data.WriteUshort( 1 ); // Color planes
	data.WriteUshort( 24 ); // Bits per pixel
	data.WriteUint( 0 ); // Compression
	data.WriteUint( m_Width * m_Height * 3 + padding * m_Height );
	data.WriteUint( 0 ); // X resolution, ignored
	data.WriteUint( 0 ); // Y resolution, ignored
	data.WriteUint( 0 ); // Palette colors
	data.WriteUint( 0 ); // Important colors

	// Pixel data
	for ( short y = m_Height - 1; y >= 0; y-- )
	{
		for ( unsigned short x = 0; x < m_Width; x++ )
		{
			Color& col = m_ImageData[ x + y * m_Width ];
			data.WriteUbyte( col.B );
			data.WriteUbyte( col.G );
			data.WriteUbyte( col.R );

			if ( x == m_Width - 1 ) data.Pad( padding );
		}
	}

	std::ofstream file( filename.c_str(), std::ios::binary );
	if ( !file.is_open() ) throw Exception("Could not load m_ImageData file!");

	file.write( (char*)data.Data(), data.Length() );

	file.close();
}

void Image::LoadTGA( ByteReader& data )
{
	// TGA header
	data.Advance( 1 ); // Image ID field length, ignored
	if ( data.ReadUbyte() != 0 ) throw Exception("Bad m_ImageData format!"); // Color map
	unsigned char type = data.ReadUbyte();
	if ( type != 2 && type != 10 ) throw Exception("Bad m_ImageData format!"); // Image type not true-color
	data.Advance( 5 ); // Color map info, ignored
	data.Advance( 4 ); // XY offset, ignored
	unsigned short width = data.ReadUshort();
	unsigned short height = data.ReadUshort();
	unsigned char depth = data.ReadUbyte();
	if ( depth != 24 && depth != 32 ) throw Exception("Bad m_ImageData format!"); // Not RGB(A)
	unsigned char bytesPerPixel = depth / 8;
	unsigned char descriptor = data.ReadUbyte();
	if ( ( depth == 24 && descriptor != 0 ) || ( depth == 32 && descriptor != 8 ) ) throw Exception("Bad m_ImageData format!"); // Check for alpha channel if bit depth is 32

	// If pixels are RLE encoded, they need to be unpacked first
	if ( type == 10 )
		DecodeRLE( data, width * height * bytesPerPixel, bytesPerPixel );
		
	// Pixel data
	m_ImageData = new Color[ width * height ];

	for ( short y = height - 1; y >= 0; y-- )
	{
		for ( unsigned short x = 0; x < width; x++ )
		{
			if ( bytesPerPixel == 3 )
				m_ImageData[ x + y * width ] = Color( data.PeekByte( 2 ), data.PeekByte( 1 ), data.PeekByte( 0 ) ); // BGR byte order
			else
				m_ImageData[ x + y * width ] = Color( data.PeekByte( 2 ), data.PeekByte( 1 ), data.PeekByte( 0 ), data.PeekByte( 3 ) ); // BGRA byte order

			data.Advance( bytesPerPixel );
		}
	}

	this->m_Width = width;
	this->m_Height = height;
}

void Image::DecodeRLE( ByteReader& data, unsigned int decodedLength, unsigned char bytesPerPixel )
{
	std::vector<unsigned char> buffer;

	while ( buffer.size() < decodedLength )
	{
		unsigned char rle = data.ReadUbyte();
		unsigned int count = ( rle & 0x7F ) + 1;

		if ( rle & 0x80 ) // RLE packet
		{
			Color value( data.PeekByte( 2 ), data.PeekByte( 1 ), data.PeekByte( 0 ) );
			if ( bytesPerPixel == 4 ) value.A = data.PeekByte( 3 );
			data.Advance( bytesPerPixel );

			for ( unsigned int i = 0; i < count; i++ )
			{
				buffer.push_back( value.B );
				buffer.push_back( value.G );
				buffer.push_back( value.R );
				if ( bytesPerPixel == 4 ) buffer.push_back( value.A );
			}
		} else { // Non-RLE packet
			for ( unsigned int i = 0; i < count * bytesPerPixel; i++ )
				buffer.push_back( data.ReadUbyte() );
		}
	}

	data.Reuse( decodedLength );
	memcpy( data.Data(), &buffer[0], decodedLength );
}

void Image::SaveTGA( const std::string& filename )
{
	ByteWriter data( true );

	// TGA header
	data.WriteUbyte( 0 ); // Image ID field length
	data.WriteUbyte( 0 ); // Color map
	data.WriteUbyte( 10 ); // Image type (true-color, RLE)
	data.Pad( 5 + 4 ); // No color map or XY offset
	data.WriteUshort( m_Width );
	data.WriteUshort( m_Height );
	data.WriteUbyte( 24 ); // Bits per pixel
	data.WriteUbyte( 0 ); // Image descriptor (No alpha depth or direction)

	// Pixel data
	std::vector<unsigned char> pixelData;
	pixelData.reserve( m_Width * m_Height * 3 );

	for ( short y = m_Height - 1; y >= 0; y-- )
	{
		for ( unsigned short x = 0; x < m_Width; x++ )
		{
			Color& col = m_ImageData[ x + y * m_Width ];
			pixelData.push_back( col.B );
			pixelData.push_back( col.G );
			pixelData.push_back( col.R );
		}
	}

	// Compress using RLE
	EncodeRLE( data, pixelData, m_Width );

	// Footer
	data.WriteUint( 0 );
	data.WriteUint( 0 );
	data.WriteString( "TRUEVISION-XFILE." );
		
	std::ofstream file( filename.c_str(), std::ios::binary );
	if ( !file.is_open() ) throw Exception("Could not load m_ImageData file!");

	file.write( (char*)data.Data(), data.Length() );

	file.close();
}

inline void flushRLE( ByteWriter& data, std::vector<Color>& backlog )
{
	if ( backlog.size() > 0 )
	{
		data.WriteUbyte( 0x80 + (unsigned char)backlog.size() - 1 );

		data.WriteUbyte( backlog[0].B );
		data.WriteUbyte( backlog[0].G );
		data.WriteUbyte( backlog[0].R );

		backlog.clear();
	}
}

inline void flushNonRLE( ByteWriter& data, std::vector<Color>& backlog, Color& lastColor )
{
	if ( backlog.size() > 1 )
	{
		data.WriteUbyte( (unsigned char)backlog.size() - 2 );

		for ( unsigned int i = 0; i < backlog.size() - 1; i++ )
		{
			data.WriteUbyte( backlog[i].B );
			data.WriteUbyte( backlog[i].G );
			data.WriteUbyte( backlog[i].R );
		}

		backlog.clear();

		backlog.push_back( lastColor );
	}
}

void Image::EncodeRLE( ByteWriter& data, std::vector<unsigned char>& pixels, unsigned int width )
{
	std::vector<Color> backlog;
	Color lastColor;
	bool rleMode = false;

	for ( unsigned int i = 0; i < pixels.size(); i += 3 )
	{
		Color col( pixels[i+2], pixels[i+1], pixels[i+0] );

		if ( !rleMode && lastColor.R == col.R && lastColor.G == col.G && lastColor.B == col.B )
		{
			flushNonRLE( data, backlog, lastColor );
			rleMode = true;
		}
		else if ( rleMode && ( lastColor.R != col.R || lastColor.G != col.G || lastColor.B != col.B ) )
		{
			flushRLE( data, backlog );
			rleMode = false;
		}
		else if ( backlog.size() == 127 || ( i % width == 0 ) )
		{
			if ( rleMode )
				flushRLE( data, backlog );
			else
				flushNonRLE( data, backlog, lastColor );
		}

		backlog.push_back( col );
		lastColor = col;
	}

	if ( backlog.size() > 0 )
	{
		if ( rleMode )
			flushRLE( data, backlog );
		else
			flushNonRLE( data, backlog, lastColor );
	}
}

void Image::LoadJPEG( ByteReader& data )
{
	// Initialize structures
	jpeg_decompress_struct cinfo;
	jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error( &jerr );
	jpeg_create_decompress( &cinfo );
	jpeg_mem_src( &cinfo, data.Data(), data.Length() );

	// JPEG header
	jpeg_read_header( &cinfo, true );
	if ( cinfo.output_width > USHRT_MAX ) throw Exception("Bad m_ImageData format!");
	if ( cinfo.output_height > USHRT_MAX ) throw Exception("Bad m_ImageData format!");

	// Pixel data
	jpeg_start_decompress( &cinfo );
		
	int stride = cinfo.output_width * cinfo.output_components;
	JSAMPARRAY buffer = cinfo.mem->alloc_sarray( (j_common_ptr)&cinfo, JPOOL_IMAGE, stride, 1 );

	m_ImageData = new Color[ cinfo.output_width * cinfo.output_height ];

	for ( unsigned short y = 0; y < cinfo.output_height; y++ )
	{
		jpeg_read_scanlines( &cinfo, buffer, 1 );
			
		for (unsigned short x = 0; x < cinfo.output_width; x++ )
			m_ImageData[ x + y * cinfo.output_width ] = Color( buffer[0][x*3+0], buffer[0][x*3+1], buffer[0][x*3+2] );
	}

	jpeg_finish_decompress( &cinfo );

	jpeg_destroy_decompress( &cinfo );

	this->m_Width = (unsigned short)cinfo.output_width;
	this->m_Height = (unsigned short)cinfo.output_height;
}

void Image::SaveJPEG( const std::string& filename )
{
	FILE* file = fopen( filename.c_str(), "wb" );
	if ( !file ) throw Exception("Could not load m_ImageData file!");

	// Initialize structures
	jpeg_compress_struct cinfo;
	jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error( &jerr );
	jpeg_create_compress( &cinfo );
		
	cinfo.image_width = m_Width;
	cinfo.image_height = m_Height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	// Configure image
	jpeg_stdio_dest( &cinfo, file );
	jpeg_set_defaults( &cinfo );
	jpeg_set_quality( &cinfo, 90, true );

	// Prepare pixel data
	std::vector<unsigned char> pixelData;
	pixelData.reserve( m_Width * m_Height * 3 );

	for (unsigned short y = 0; y < m_Height; y++ )
	{
		for (unsigned short x = 0; x < m_Width; x++ )
		{
			Color& col = m_ImageData[ x + y * m_Width ];
			pixelData.push_back( col.R );
			pixelData.push_back( col.G );
			pixelData.push_back( col.B );
		}
	}

	// Compress
	jpeg_start_compress( &cinfo, true );

	for (unsigned short y = 0; y < m_Height; y++ )
	{
		JSAMPROW row = &pixelData[ y * m_Width * 3 ];
		jpeg_write_scanlines( &cinfo, &row, 1 );
	}

	jpeg_finish_compress( &cinfo );

	jpeg_destroy_compress( &cinfo );

	fclose( file );
}

void readPNG( png_structp png_ptr, png_bytep dest, png_size_t length )
{
	ByteReader& data = *(ByteReader*)png_ptr->io_ptr;
	data.Read( dest, (unsigned int)length );
}

void Image::LoadPNG( ByteReader& data )
{		
	// Initialize structures
	png_structp png = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	png_infop info = png_create_info_struct( png );

	setjmp( png_jmpbuf( png ) );

	png_set_read_fn( png, (void*)&data, &readPNG );

	// Header
	png_read_info( png, info );
	if ( info->width > USHRT_MAX ) throw Exception("Bad m_ImageData format!");
	if ( info->height > USHRT_MAX ) throw Exception("Bad m_ImageData format!");

	// Pixel data
	m_ImageData = new Color[ info->width * info->height ];

	png_uint_32 rowLength = png_get_rowbytes( png, info );
	std::vector<unsigned char> row( rowLength );

	for (unsigned short y = 0; y < info->height; y++ )
	{
		png_read_row( png, &row[0], NULL );

		if ( info->color_type == PNG_COLOR_TYPE_RGB )
			for (unsigned short x = 0; x < info->width; x++ )
				m_ImageData[ x + y * info->width ] = Color( row[x*3+0], row[x*3+1], row[x*3+2] );
		else if ( info->color_type == PNG_COLOR_TYPE_RGBA )
			for (unsigned short x = 0; x < info->width; x++ )
				m_ImageData[ x + y * info->width ] = Color( row[x*4+0], row[x*4+1], row[x*4+2], row[x*4+3] );
		else
			throw Exception("Bad m_ImageData format!");
	}

	m_Width = (unsigned short)info->width;
	m_Height = (unsigned short)info->height;

	png_destroy_read_struct( &png, &info, NULL );
}

void Image::SavePNG( const std::string& filename )
{
	FILE* file = fopen( filename.c_str(), "wb" );
	if ( !file ) throw Exception("Could not load m_ImageData file!");

	// Initialize structures
	png_structp png = png_create_write_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	png_infop info = png_create_info_struct( png );

	setjmp( png_jmpbuf( png ) );

	// Configure image
	png_init_io( png, file );
	png_set_IHDR( png, info, m_Width, m_Height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE );
	png_write_info( png, info );

	// Prepare pixel data
	std::vector<unsigned char> pixelData;
	pixelData.reserve( m_Width * m_Height * 4 );

	for (unsigned short y = 0; y < m_Height; y++ )
	{
		for (unsigned short x = 0; x < m_Width; x++ )
		{
			Color& col = m_ImageData[ x + y * m_Width ];
			pixelData.push_back( col.R );
			pixelData.push_back( col.G );
			pixelData.push_back( col.B );
			pixelData.push_back( col.A );
		}
	}

	// Write rows
	for (unsigned short y = 0; y < m_Height; y++ )
		png_write_row( png, &pixelData[ y * m_Width * 4 ] );

	png_write_end( png, info );

	png_destroy_write_struct( &png, &info );

	fclose( file );
}