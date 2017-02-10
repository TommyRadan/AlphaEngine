#include "PngImporter.hpp"

#include <fstream>
#include <Utilities/Exception.hpp>
#include <Utilities/ByteBuffer.hpp>

#include "../libpng/png.h"

static void readPNG(png_structp png_ptr, png_bytep dest, png_size_t length)
{
    ByteReader& data = *(ByteReader*)png_ptr->io_ptr;
    data.Read( dest, (unsigned int)length );
}

Image PngImporter::Load(const std::string& filename)
{
    // Load data from file
    std::ifstream file( filename.c_str(), std::ios::binary | std::ios::ate );
    if (!file.is_open()) throw Exception("Could not load file!");

    unsigned int fileSize = (unsigned int)file.tellg();
    file.seekg( 0, std::ios::beg );

    ByteReader data( fileSize, true );
    file.read( (char*)data.Data(), fileSize );

    file.close();

    // Initialize structures
    png_structp png = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
    png_infop info = png_create_info_struct( png );

    setjmp( png_jmpbuf( png ) );

    png_set_read_fn( png, (void*)&data, &readPNG );

    // Header
    png_read_info( png, info );
    if ( info->width > USHRT_MAX ) throw Exception("Bad PNG format!");
    if ( info->height > USHRT_MAX ) throw Exception("Bad PNG format!");

    // Pixel data
    Color* const imageData = new Color[info->width * info->height];

    png_uint_32 rowLength = png_get_rowbytes(png, info);
    std::vector<unsigned char> row(rowLength);

    for (unsigned short y = 0; y < info->height; y++ )
    {
        png_read_row( png, &row[0], NULL );

        if ( info->color_type == PNG_COLOR_TYPE_RGB )
            for (unsigned short x = 0; x < info->width; x++ )
                imageData[ x + y * info->width ] = Color( row[x*3+0], row[x*3+1], row[x*3+2] );
        else if ( info->color_type == PNG_COLOR_TYPE_RGBA )
            for (unsigned short x = 0; x < info->width; x++ )
                imageData[ x + y * info->width ] = Color( row[x*4+0], row[x*4+1], row[x*4+2], row[x*4+3] );
        else
            throw Exception("Bad m_ImageData format!");
    }

    unsigned short width = (unsigned short)info->width;
    unsigned short height = (unsigned short)info->height;
    png_destroy_read_struct( &png, &info, NULL );

    return Image(width, height, imageData);
}