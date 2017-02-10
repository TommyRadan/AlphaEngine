#pragma once

#include "../Image.hpp"

#include <string>

#include <Utilities/Exception.hpp>
#include <Utilities/ByteBuffer.hpp>

struct BmpImporter
{
    static Image Load(const std::string& filename);

private:
    static bool IsBMP(const ByteReader& data);
    static void ParseHeader(ByteReader& data, unsigned int& height, unsigned int& width, int& rawHeight);
};
