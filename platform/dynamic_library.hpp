// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file dynamic_library.hpp
 * @brief Loading a shared library and looking up its symbols.
 */

#pragma once

#include <filesystem>
#include <string>

namespace platform
{
    /**
     * @brief A shared library (`.dll` / `.so` / `.dylib`) opened for symbol
     *        lookup — the hook a plugin or a hot-reloadable game module
     *        would load through.
     *
     * Move-only: the handle is unloaded exactly once, by the destructor or
     * @ref close. A failed @ref open leaves the object closed and records the
     * platform's reason in @ref last_error; nothing here throws or logs, so
     * the caller decides how loud a missing optional library should be.
     */
    struct dynamic_library
    {
        dynamic_library() = default;

        /** @brief Opens @p path; check @ref is_open. */
        explicit dynamic_library(const std::filesystem::path& path);

        ~dynamic_library();

        dynamic_library(const dynamic_library&) = delete;
        dynamic_library& operator=(const dynamic_library&) = delete;
        dynamic_library(dynamic_library&& other) noexcept;
        dynamic_library& operator=(dynamic_library&& other) noexcept;

        /**
         * @brief Loads the library at @p path, closing any library this object
         *        already holds first.
         * @return true on success; false with @ref last_error set otherwise.
         */
        bool open(const std::filesystem::path& path);

        /** @brief Unloads the library, if one is open. */
        void close() noexcept;

        /** @brief Whether a library is loaded. */
        bool is_open() const noexcept;

        /**
         * @brief The address of the exported symbol @p name, or @c nullptr when
         *        the library is closed or exports no such symbol (then
         *        @ref last_error says why).
         */
        void* symbol(const char* name);

        /** @brief The reason the last @ref open or @ref symbol failed; empty when it succeeded. */
        const std::string& last_error() const noexcept;

    private:
        void* m_handle{nullptr};
        std::string m_last_error;
    };
} // namespace platform
