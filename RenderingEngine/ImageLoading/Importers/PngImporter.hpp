#pragma once

#include "../Image.hpp"

#include <string>

#include <Utilities/Exception.hpp>

struct PngImporter
{
    static Image Load(const std::string&);
};
