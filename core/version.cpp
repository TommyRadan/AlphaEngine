// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/version.hpp>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#ifndef VERSION_MAJOR
#warning VERSION_MAJOR not defined, setting to default value ...
#define VERSION_MAJOR 0
#endif

#ifndef VERSION_MINOR
#warning VERSION_MINOR not defined, setting to default value ...
#define VERSION_MINOR 0
#endif

#ifndef VERSION_PATCH
#warning VERSION_PATCH not defined, setting to default value ...
#define VERSION_PATCH 0
#endif

const std::string core::version::get_version()
{
    return std::string{STR(VERSION_MAJOR) + std::string{"."} + STR(VERSION_MINOR) + std::string{"."} +
                       STR(VERSION_PATCH)};
}

const std::string core::version::get_build_date()
{
    return std::string{__DATE__};
}
