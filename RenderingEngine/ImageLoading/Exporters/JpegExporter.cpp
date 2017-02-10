#include "JpegExporter.hpp"

#include <cstdio>
#include <vector>

#include "../libjpeg/jpeglib.h"

void JpegExporter::Save(const std::string filename, const Image& image)
{
    unsigned int width = image.GetWidth();
    unsigned int height = image.GetHeight();
    const Color* const imageData = image.GetPixels();

    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) throw Exception("Could not load m_ImageData file!");

    // Initialize structures
    jpeg_compress_struct cinfo;
    jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    // Configure image
    jpeg_stdio_dest(&cinfo, file);
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, 0x01);

    // Prepare pixel data
    std::vector<unsigned char> pixelData;
    pixelData.reserve(width * height * 3);

    for (unsigned short y = 0u; y < height; ++y) {
        for (unsigned short x = 0u; x < width; ++x) {
            const Color& col = imageData[x + y * width];
            pixelData.push_back(col.R);
            pixelData.push_back(col.G);
            pixelData.push_back(col.B);
        }
    }

    // Compress
    jpeg_start_compress(&cinfo, 0x01);

    for (unsigned short y = 0; y < height; ++y) {
        JSAMPROW row = &pixelData[y * width * 3];
        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(file);
}
