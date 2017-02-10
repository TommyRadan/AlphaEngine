#pragma once

#include "../Image.hpp"

#include <string>

#include <Utilities/Exception.hpp>


struct JpegExporter
{
    void Save(const std::string filename, const Image& image);
};
