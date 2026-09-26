/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file platform.hpp
 * @brief The operating-system services the engine core depends on: the
 *        clock, the well-known paths, file I/O, the environment, native
 *        log capture and the crash handler.
 *
 * Everything outside this directory is written against these functions
 * rather than against SDL (or any other OS library), so `core` compiles
 * without naming SDL: `core/time`, `core/log` and `core/settings` reach the
 * performance counter, the executable's directory and the per-user
 * preference directory through here. The SDL-backed implementation lives
 * in platform_sdl.cpp; the parts that need only the C++ standard library
 * (file I/O, local time, path folding, the crash hook) in platform.cpp.
 * Directory watching and dynamic libraries have headers of their own
 * (directory_watcher.hpp, dynamic_library.hpp).
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

namespace core::platform
{
    // -- Clock ---------------------------------------------------------------

    /** @brief The high-resolution performance counter, in ticks of @ref performance_frequency. */
    uint64_t performance_counter();

    /** @brief Ticks per second of @ref performance_counter. Never zero. */
    uint64_t performance_frequency();

    /** @brief Milliseconds since the platform library was initialised. */
    uint64_t ticks_ms();

    /**
     * @brief Breaks @p when down into local calendar time.
     * The thread-safe variant of @c localtime on every platform
     * (@c localtime_s on Windows, @c localtime_r elsewhere).
     * @return false, leaving @p out untouched, when the conversion fails.
     */
    bool local_time(std::time_t when, std::tm& out);

    // -- Paths ---------------------------------------------------------------

    /**
     * @brief The directory the running executable lives in, or an empty path
     *        when the platform cannot say. Without a trailing separator.
     */
    std::filesystem::path base_path();

    /**
     * @brief The per-user, writable preference directory for @p application
     *        under @p organization (created on demand), or an empty path when
     *        the platform has none. Settings and caches live here.
     */
    std::filesystem::path pref_path(const char* organization, const char* application);

    /**
     * @brief Where the engine's loose asset files are: the directory the
     *        default VFS mount points at (see core/vfs/vfs.hpp).
     * Resolved once through @ref locate_asset_root from @ref base_path.
     * This is the discovered default only; the settings layer's
     * @c assets.root (or @c ALPHAENGINE_ASSET_ROOT) overrides it in the
     * engine.
     */
    std::filesystem::path asset_root();

    /**
     * @brief The pure part of @ref asset_root: the first existing
     *        @c assets directory in @p base_path or any of its parents
     *        (so a binary under @c Binaries/Debug/ finds the repository's),
     *        else @c <base_path>/assets even though it does not exist.
     */
    std::filesystem::path locate_asset_root(const std::filesystem::path& base_path);

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

    // -- Native log capture --------------------------------------------------

    /** @brief Severity of a message the platform library emitted on its own; mirrors @c core::logging::verbosity. */
    enum class native_log_level
    {
        trace,
        debug,
        info,
        warn,
        error,
        fatal
    };

    /** @brief Receives the platform library's own log messages (see @ref set_native_log_sink). */
    using native_log_sink = void (*)(native_log_level level, const char* message);

    /**
     * @brief Routes the messages the platform library emits itself (SDL's
     *        diagnostics) into @p sink, so they land in the engine log
     *        alongside everything else. A null sink restores the default.
     */
    void set_native_log_sink(native_log_sink sink);

    /** @brief The least severe native message the platform library should bother formatting and delivering. */
    void set_native_log_level(native_log_level minimum);

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
     *        @c SIGFPE, @c SIGILL) and for @c std::terminate. After the handler
     *        returns the default disposition is restored and the signal
     *        re-raised, so the OS still produces its crash report / core dump.
     *        A null handler uninstalls.
     */
    void install_crash_handler(crash_handler handler);
} // namespace core::platform
