#include "JpegImporter.hpp"

#include <fstream>
#include <Utilities/ByteBuffer.hpp>
#include "../libjpeg/jpeglib.h"

Image JpegImporter::Load(const std::string& filename)
{
    // Load data from file
    std::ifstream file( filename.c_str(), std::ios::binary | std::ios::ate );
    if (!file.is_open()) throw Exception("Could not load m_ImageData file!");

    unsigned int fileSize = (unsigned int)file.tellg();
    file.seekg(0, std::ios::beg);

    ByteReader data(fileSize, true);
    file.read((char*)data.Data(), fileSize);

    file.close();

    // Initialize structures
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data.Data(), data.Length());

    // JPEG header
    jpeg_read_header(&cinfo, 0x01);
    if (cinfo.output_width > USHRT_MAX) throw Exception("Bad m_ImageData format!");
    if (cinfo.output_height > USHRT_MAX) throw Exception("Bad m_ImageData format!");

    // Pixel data
    jpeg_start_decompress(&cinfo);

    int stride = cinfo.output_width * cinfo.output_components;
    JSAMPARRAY buffer = cinfo.mem->alloc_sarray((j_common_ptr)&cinfo, JPOOL_IMAGE, stride, 1);

    Color* const imageData = new Color[cinfo.output_width * cinfo.output_height];

    for (unsigned short y = 0u; y < cinfo.output_height; ++y) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        for (unsigned short x = 0u; x < cinfo.output_width; ++x) {
            imageData[x + y * cinfo.output_width] = Color(buffer[0][3*x + 0], buffer[0][3*x + 1],  buffer[0][3*x + 2]);
        }
    }

    unsigned short width = (unsigned short)cinfo.output_width;
    unsigned short height = (unsigned short)cinfo.output_height;

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return Image(width, height, imageData);
}
