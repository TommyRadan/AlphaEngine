// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/platform/platform.hpp>

#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <system_error>

// The parts of the platform layer that need only the C++ standard library:
// file I/O, local time, path folding, the asset-root search and the crash
// hook. The SDL-backed services are in platform_sdl.cpp.

namespace core::platform
{
    namespace
    {
        // The installed crash handler, read from inside a signal handler, so it
        // is an atomic rather than a guarded object.
        std::atomic<crash_handler> g_crash_handler{nullptr};
        std::terminate_handler g_previous_terminate = nullptr;

        const char* signal_name(int signal)
        {
            switch (signal)
            {
            case SIGSEGV:
                return "SIGSEGV";
            case SIGABRT:
                return "SIGABRT";
            case SIGFPE:
                return "SIGFPE";
            case SIGILL:
                return "SIGILL";
            default:
                return "signal";
            }
        }

        void on_fatal_signal(int signal)
        {
            if (const crash_handler handler = g_crash_handler.load(std::memory_order_relaxed); handler != nullptr)
            {
                handler(signal_name(signal));
            }
            // Hand the signal back to the OS so it produces its own report /
            // core dump instead of the process silently continuing.
            std::signal(signal, SIG_DFL);
            std::raise(signal);
        }

        void on_terminate()
        {
            if (const crash_handler handler = g_crash_handler.load(std::memory_order_relaxed); handler != nullptr)
            {
                handler("std::terminate");
            }
            if (g_previous_terminate != nullptr)
            {
                g_previous_terminate();
            }
            std::abort();
        }

        constexpr int k_fatal_signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL};
    } // namespace

    bool local_time(std::time_t when, std::tm& out)
    {
#if defined(_WIN32)
        return localtime_s(&out, &when) == 0;
#else
        return localtime_r(&when, &out) != nullptr;
#endif
    }

    std::filesystem::path asset_root()
    {
        static const std::filesystem::path root = locate_asset_root(base_path());
        return root;
    }

    std::filesystem::path locate_asset_root(const std::filesystem::path& base_path)
    {
        std::error_code error;
        for (std::filesystem::path directory = base_path; !directory.empty(); directory = directory.parent_path())
        {
            const std::filesystem::path candidate = directory / "assets";
            if (std::filesystem::is_directory(candidate, error))
            {
                return candidate;
            }
            if (directory.parent_path() == directory)
            {
                break;
            }
        }
        return base_path / "assets";
    }

    std::filesystem::path current_directory()
    {
        std::error_code error;
        std::filesystem::path directory = std::filesystem::current_path(error);
        return error ? std::filesystem::path{} : directory;
    }

    std::filesystem::path utf8_path(std::string_view utf8)
    {
        return std::filesystem::path{std::u8string(utf8.begin(), utf8.end())};
    }

    std::string path_to_utf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    bool case_insensitive_paths()
    {
#if defined(_WIN32) || defined(__APPLE__)
        return true;
#else
        return false;
#endif
    }

    std::string fold_path_case(std::string path)
    {
        for (char& c : path)
        {
            const unsigned char byte = static_cast<unsigned char>(c);
            if (byte < 0x80)
            {
                c = static_cast<char>(std::tolower(byte));
            }
        }
        return path;
    }

    std::optional<std::string> environment_variable(const char* name)
    {
        if (name == nullptr)
        {
            return std::nullopt;
        }
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            return std::nullopt;
        }
        return std::string{value};
    }

    bool read_file(const std::filesystem::path& path, std::vector<std::byte>& out, std::string* error)
    {
        out.clear();
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file.is_open())
        {
            if (error != nullptr)
            {
                *error = "cannot open file";
            }
            return false;
        }
        const std::streamoff size = file.tellg();
        if (size < 0)
        {
            if (error != nullptr)
            {
                *error = "cannot determine file size";
            }
            return false;
        }
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
        {
            out.clear();
            if (error != nullptr)
            {
                *error = "read failed";
            }
            return false;
        }
        return true;
    }

    bool read_text_file(const std::filesystem::path& path, std::string& out, std::string* error)
    {
        std::vector<std::byte> bytes;
        if (!read_file(path, bytes, error))
        {
            out.clear();
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }

    bool write_file(const std::filesystem::path& path, const void* data, std::size_t size, std::string* error)
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        if (!file.is_open())
        {
            if (error != nullptr)
            {
                *error = "cannot create file";
            }
            return false;
        }
        if (size > 0 && !file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)))
        {
            if (error != nullptr)
            {
                *error = "write failed";
            }
            return false;
        }
        file.flush();
        if (!file)
        {
            if (error != nullptr)
            {
                *error = "flush failed";
            }
            return false;
        }
        return true;
    }

    bool file_exists(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    }

    std::optional<std::filesystem::file_time_type> last_write_time(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
        if (error)
        {
            return std::nullopt;
        }
        return time;
    }

    void install_crash_handler(crash_handler handler)
    {
        static bool installed = false;
        g_crash_handler.store(handler, std::memory_order_relaxed);
        if (handler != nullptr)
        {
            for (const int signal : k_fatal_signals)
            {
                std::signal(signal, &on_fatal_signal);
            }
            if (!installed)
            {
                // Chain to whatever terminate handler was in place, once: a
                // second install must not record our own handler as the
                // previous one and recurse.
                g_previous_terminate = std::set_terminate(&on_terminate);
                installed = true;
            }
        }
        else
        {
            for (const int signal : k_fatal_signals)
            {
                std::signal(signal, SIG_DFL);
            }
            if (installed && g_previous_terminate != nullptr)
            {
                std::set_terminate(g_previous_terminate);
            }
            g_previous_terminate = nullptr;
            installed = false;
        }
    }
} // namespace core::platform
