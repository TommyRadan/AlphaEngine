#include "BmpExporter.hpp"

#include <fstream>
#include <Utilities/ByteBuffer.hpp>

void BmpExporter::Save(const std::string filename, const Image& image)
{
    unsigned int width = image.GetWidth();
    unsigned int height = image.GetHeight();
    const Color* const imageData = image.GetPixels();

    ByteWriter data(true);
    unsigned int padding = (width * 3) % 4;

    // BMP header
    data.WriteUbyte('B');
    data.WriteUbyte('M');
    data.WriteUint(width * height * 3 + padding * height + 54); // File size
    data.WriteUint(0); // Two application specific shorts
    data.WriteUint(54); // Offset to pixel array

    // DIB header
    data.WriteUint(40); // Header size
    data.WriteUint(width);
    data.WriteUint(height);
    data.WriteUshort(1); // Color planes
    data.WriteUshort(24); // Bits per pixel
    data.WriteUint(0); // Compression
    data.WriteUint(width * height * 3 + padding * height);
    data.WriteUint(0); // X resolution, ignored
    data.WriteUint(0); // Y resolution, ignored
    data.WriteUint(0); // Palette colors
    data.WriteUint(0); // Important colors

    /*
     * TODO: This pixel copy is not needed, why not copy from image directly into file
     *       instead of copying to ByteWriter and then to file
     */

    // Pixel data
    for (int y = height - 1; y >= 0; --y) {
        for(unsigned short x = 0; x < width; ++x) {
            const Color& col = imageData[x + y*width];
            data.WriteUbyte(col.B);
            data.WriteUbyte(col.G);
            data.WriteUbyte(col.R);

            if (x == width - 1) data.Pad(padding);
        }
    }

    std::ofstream file( filename.c_str(), std::ios::binary );
    if ( !file.is_open() ) throw Exception("Could not create file!");
    file.write( (char*)data.Data(), data.Length() );
    file.close();
}
