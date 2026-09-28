// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file os.hpp
 * @brief The operating-system services core itself relies on, written
 *        against the C++ standard library alone: local time, file I/O,
 *        UTF-8 paths, the environment and the crash handler.
 *
 * `core` never includes an OS library: `core/log`, `core/settings` and the
 * VFS reach files, paths and the environment through here. The services
 * that need one — the executable and preference directories, the content
 * root, the window, audio devices, dynamic libraries — belong to the
 * top-level platform module (platform/platform.hpp), which the engine
 * hands to core where core needs them. Directory watching has a header of
 * its own (directory_watcher.hpp).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core::os
{
    // -- Time ----------------------------------------------------------------

    /**
     * @brief Breaks @p when down into local calendar time.
     * The thread-safe variant of @c localtime on every platform
     * (@c localtime_s on Windows, @c localtime_r elsewhere).
     * @return false, leaving @p out untouched, when the conversion fails.
     */
    bool local_time(std::time_t when, std::tm& out);

    // -- Paths ---------------------------------------------------------------

    /** @brief The process's current working directory, or an empty path on failure. */
    std::filesystem::path current_directory();

    /**
     * @brief Builds a path from UTF-8 text.
     * The OS hands paths back as UTF-8; going through @c char8_t keeps that
     * meaning on Windows, where a narrow @c std::filesystem::path would be
     * read in the ANSI code page.
     */
    std::filesystem::path utf8_path(std::string_view utf8);

    /** @brief The UTF-8 spelling of @p path, the inverse of @ref utf8_path. */
    std::string path_to_utf8(const std::filesystem::path& path);

    /**
     * @brief Whether the platform's filesystems compare paths without regard
     *        to case (Windows, macOS). Cache keys are folded through
     *        @ref fold_path_case when this is true so two spellings of one
     *        file share an entry.
     */
    bool case_insensitive_paths();

    /**
     * @brief Lower-cases the ASCII letters of @p path. Non-ASCII bytes pass
     *        through unchanged: a full Unicode fold would need the OS's
     *        collation tables, and ASCII covers every path the engine ships.
     */
    std::string fold_path_case(std::string path);

    // -- Environment ---------------------------------------------------------

    /** @brief The value of the environment variable @p name, or @c std::nullopt when it is unset. */
    std::optional<std::string> environment_variable(const char* name);

    // -- Files ---------------------------------------------------------------

    /**
     * @brief Reads the whole file at @p path into @p out.
     * @param error When non-null, receives a one-line reason on failure.
     * @return false when the file cannot be opened or read; @p out is then
     *         left empty.
     */
    bool read_file(const std::filesystem::path& path, std::vector<std::byte>& out, std::string* error = nullptr);

    /** @brief @ref read_file into a string. */
    bool read_text_file(const std::filesystem::path& path, std::string& out, std::string* error = nullptr);

    /**
     * @brief Writes @p size bytes from @p data to @p path, creating or
     *        truncating it (the parent directory must exist).
     * @return false when the file cannot be created or fully written.
     */
    bool
    write_file(const std::filesystem::path& path, const void* data, std::size_t size, std::string* error = nullptr);

    /** @brief Whether a regular file exists at @p path. Never throws. */
    bool file_exists(const std::filesystem::path& path);

    /** @brief The last modification time of @p path, or @c std::nullopt when it cannot be read. Never throws. */
    std::optional<std::filesystem::file_time_type> last_write_time(const std::filesystem::path& path);

    // -- Crash handler -------------------------------------------------------

    /**
     * @brief Called from the crash path with a short reason (@c "SIGSEGV",
     *        @c "std::terminate", ...). It runs inside a signal handler, so
     *        it must not allocate, lock or log through the engine logger —
     *        a @c fputs to stderr is the safe extent of it.
     */
    using crash_handler = void (*)(const char* reason);

    /**
     * @brief Installs @p handler for the fatal signals (@c SIGSEGV, @c SIGABRT,
     *        @c SIGFPE, @c SIGILL) and for @c std::terminate. Before the handler
     *        runs, the log's buffered lines are written out
     *        (@c core::logging::flush_after_crash). After the handler
     *        returns the default disposition is restored and the signal
     *        re-raised, so the OS still produces its crash report / core dump.
     *        A null handler uninstalls.
     */
    void install_crash_handler(crash_handler handler);
} // namespace core::os
