// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/vfs/directory_mount.hpp>

#include <system_error>
#include <utility>

#include <core/os/os.hpp>

namespace core
{
    directory_mount::directory_mount(std::filesystem::path root) : m_root{std::move(root)} {}

    const std::filesystem::path& directory_mount::root() const noexcept
    {
        return m_root;
    }

    std::string directory_mount::describe() const
    {
        return "directory " + os::path_to_utf8(m_root);
    }

    bool directory_mount::exists(const std::string& relative) const
    {
        return os::file_exists(native_path(relative));
    }

    bool directory_mount::read(const std::string& relative, std::vector<std::byte>& out, std::string& error) const
    {
        return os::read_file(native_path(relative), out, &error);
    }

    std::filesystem::path directory_mount::native_path(const std::string& relative) const
    {
        return m_root / os::utf8_path(relative);
    }

    std::optional<std::filesystem::file_time_type> directory_mount::last_write_time(const std::string& relative) const
    {
        return os::last_write_time(native_path(relative));
    }

    bool directory_mount::writable() const
    {
        return true;
    }

    bool directory_mount::write(const std::string& relative, const void* data, std::size_t size, std::string& error)
    {
        const std::filesystem::path target = native_path(relative);
        if (target.has_parent_path())
        {
            std::error_code created;
            std::filesystem::create_directories(target.parent_path(), created);
            if (created)
            {
                error = "cannot create directory " + os::path_to_utf8(target.parent_path()) + ": " + created.message();
                return false;
            }
        }
        return os::write_file(target, data, size, &error);
    }
} // namespace core
