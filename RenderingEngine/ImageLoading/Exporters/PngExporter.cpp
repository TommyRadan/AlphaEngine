#include "PngExporter.hpp"

#include <cstdio>
#include <vector>

#include "../libpng/png.h"

void PngExporter::Save(const std::string filename, const Image& image)
{
    unsigned int width = image.GetWidth();
    unsigned int height = image.GetHeight();
    const Color* const imageData = image.GetPixels();

    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) throw Exception("Could not create file!");

    // Initialize structures
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);

    setjmp(png_jmpbuf(png));

    // Configure image
    png_init_io(png, file);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png, info);

    // Prepare pixel data
    std::vector<unsigned char> pixelData;
    pixelData.reserve(width * height * 4);

    for (unsigned short y = 0; y < height; ++y) {
        for (unsigned short x = 0; x < width; ++x) {
            const Color& col = imageData[x + y * width];
            pixelData.push_back(col.R);
            pixelData.push_back(col.G);
            pixelData.push_back(col.B);
            pixelData.push_back(col.A);
        }
    }

    // Write rows
    for (unsigned short y = 0; y < height; y++ ) {
        png_write_row(png, &pixelData[y * width * 4]);
    }

    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(file);
}
