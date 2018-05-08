#pragma once

#include <stdexcept>
#include <string>

struct Exception : std::runtime_error
{
    explicit Exception(const std::string& m) : std::runtime_error { m.c_str() } {}
};
