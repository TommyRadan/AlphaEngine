#pragma once

#include "../Image.hpp"

#include <Utilities/Exception.hpp>

struct BmpExporter
{
    static void Save(const std::string filename, const Image& image);
};