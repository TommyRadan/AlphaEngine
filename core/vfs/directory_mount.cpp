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

#include <core/vfs/directory_mount.hpp>

#include <system_error>
#include <utility>

#include <core/platform/platform.hpp>

namespace core
{
    directory_mount::directory_mount(std::filesystem::path root) : m_root{std::move(root)} {}

    const std::filesystem::path& directory_mount::root() const noexcept
    {
        return m_root;
    }

    std::string directory_mount::describe() const
    {
        return "directory " + platform::path_to_utf8(m_root);
    }

    bool directory_mount::exists(const std::string& relative) const
    {
        return platform::file_exists(native_path(relative));
    }

    bool directory_mount::read(const std::string& relative, std::vector<std::byte>& out, std::string& error) const
    {
        return platform::read_file(native_path(relative), out, &error);
    }

    std::filesystem::path directory_mount::native_path(const std::string& relative) const
    {
        return m_root / platform::utf8_path(relative);
    }

    std::optional<std::filesystem::file_time_type> directory_mount::last_write_time(const std::string& relative) const
    {
        return platform::last_write_time(native_path(relative));
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
                error = "cannot create directory " + platform::path_to_utf8(target.parent_path()) + ": " +
                        created.message();
                return false;
            }
        }
        return platform::write_file(target, data, size, &error);
    }
} // namespace core
