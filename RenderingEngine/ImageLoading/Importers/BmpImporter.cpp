#include "BmpImporter.hpp"
#include <fstream>
#include <cmath>

Image BmpImporter::Load(const std::string& filename)
{
    // Load data from file
    std::ifstream file(filename.c_str(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) throw Exception("Could not load BMP file!");

    unsigned int fileSize {(unsigned int)file.tellg()};
    file.seekg(0, std::ios::beg);

    ByteReader data(fileSize, true);
    file.read((char*)data.Data(), fileSize);
    file.close();

    if(!IsBMP(data)) throw Exception("Invalid BMP format!");

    // BMP header
    data.Advance( 2 + 4 + 4 ); // Skip magic number, file size and application specific data
    unsigned int pixelOffset = data.ReadUint();

    unsigned int height, width;
    int rawHeight;
    ParseHeader(data, height, width, rawHeight);

    // Pixel data
    data.Move( pixelOffset );
    unsigned int padding = ( width * 3 ) % 4;

    auto imageData = new Color[ width * height ];

    for (unsigned short y = 0; y < height; y++) {
        for (unsigned short x = 0; x < width; x++) {
            int o = rawHeight > 0 ? x + (height - y - 1) * width : x + y * width; // Flip image vertically if height is negative
            imageData[o] = Color(data.PeekByte(2), data.PeekByte(1), data.PeekByte(0)); // BGR byte order
            data.Advance(3);
            if (x == width - 1) data.Advance(padding);
        }
    }

    return Image(width, height, imageData);
}

bool BmpImporter::IsBMP(const ByteReader& data)
{
    return (data.PeekByte(0) == 'B' && data.PeekByte(1) == 'M');
}

void BmpImporter::ParseHeader(ByteReader& data, unsigned int& height, unsigned int& width, int& rawHeight)
{
    if ( data.ReadUint() != 40 ) throw Exception("Invalid BMP format!"); // Only version 1 is currently supported
    width = data.ReadUint();
    rawHeight = data.ReadInt();
    height = (unsigned int)(rawHeight >= 0 ? rawHeight : -rawHeight);
    if ( width == 0 || height == 0 ) throw Exception("Invalid BMP format!");
    if ( width > USHRT_MAX || height > USHRT_MAX ) throw Exception("Invalid BMP format!");
    if ( data.ReadUshort() != 1 ) throw Exception("Invalid BMP format!"); // Color planes
    if ( data.ReadUshort() != 24 ) throw Exception("Invalid BMP format!"); // Bits per pixel
    if ( data.ReadUint() != 0 ) throw Exception("Invalid BMP format!"); // Compression
    data.Advance( 4 ); // Skip pixel array size (very unreliable, a value of 0 is not uncommon)
    data.Advance( 4 + 4 ); // Skip X/Y resolution
    if ( data.ReadUint() != 0 ) throw Exception("Invalid BMP format!"); // Palette colors
    if ( data.ReadUint() != 0 ) throw Exception("Invalid BMP format!"); // Important colors
}
