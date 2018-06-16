#pragma once

#include <string>

namespace Infrastructure
{
    struct Version
    {
        static const std::string GetVersion();
        static const std::string GetBuildDate();
    };
}