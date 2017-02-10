#pragma once

#include "../Image.hpp"

#include <string>

#include <Utilities/Exception.hpp>

struct JpegImporter
{
    static Image Load(const std::string& filename);
};
