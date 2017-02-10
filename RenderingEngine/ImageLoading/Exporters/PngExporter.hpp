#pragma once

#include "../Image.hpp"

#include <string>

#include <Utilities/Exception.hpp>

struct PngExporter
{
    static void Save(const std::string filename, const Image& image);
};
