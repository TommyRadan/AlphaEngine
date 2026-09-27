// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "log.hpp"
#include <core/log.hpp>

void print_info(const std::string& message)
{
    LOG_INF("%s", message.c_str());
}

void print_warning(const std::string& message)
{
    LOG_WRN("%s", message.c_str());
}

void print_error(const std::string& message)
{
    LOG_ERR("%s", message.c_str());
}
