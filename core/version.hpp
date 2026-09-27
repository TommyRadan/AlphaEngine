// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file version.hpp
 * @brief Compile-time version information for the engine.
 */

#pragma once

#include <string>

namespace core
{
    /**
     * @brief Static accessors for build-time version metadata.
     *
     * The version triplet is baked in from the @c VERSION_MAJOR /
     * @c VERSION_MINOR / @c VERSION_PATCH preprocessor macros (set by
     * CMake), and the build date is captured from @c __DATE__.
     */
    struct version
    {
        /** @brief Returns the version string in @c "MAJOR.MINOR.PATCH" form. */
        static const std::string get_version();

        /** @brief Returns the build date captured at compile time. */
        static const std::string get_build_date();
    };
} // namespace core