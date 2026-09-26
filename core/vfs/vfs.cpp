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

#include <core/vfs/vfs.hpp>

#include <mutex>
#include <system_error>
#include <utility>

#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/directory_mount.hpp>

namespace core
{
    bool vfs::is_native(const std::filesystem::path& path)
    {
        // A root-relative path ("/textures/x.png", "C:x" on Windows) names a
        // place on the native filesystem too, never a mount entry.
        return path.is_absolute() || path.has_root_path();
    }

    std::optional<std::string> vfs::mount_relative(const std::filesystem::path& path)
    {
        const std::filesystem::path normal = path.lexically_normal();
        std::string text = normal.generic_string();
        if (text.empty() || text == "." || text == ".." || text.starts_with("../"))
        {
            return std::nullopt;
        }
        return text;
    }

    void vfs::mount(std::unique_ptr<vfs_mount> mount)
    {
        if (mount == nullptr)
        {
            return;
        }
        LOG_INF("VFS: mounted %s", mount->describe().c_str());
        std::unique_lock lock{m_mutex};
        m_mounts.push_back(std::move(mount));
    }

    void vfs::mount_directory(const std::filesystem::path& root)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
        {
            LOG_INF("VFS: asset directory %s does not exist yet; relative asset paths fall back to the working "
                    "directory until it does",
                    platform::path_to_utf8(root).c_str());
        }
        mount(std::make_unique<directory_mount>(root));
    }

    void vfs::unmount_all()
    {
        std::unique_lock lock{m_mutex};
        m_mounts.clear();
    }

    std::size_t vfs::mount_count() const
    {
        std::shared_lock lock{m_mutex};
        return m_mounts.size();
    }

    bool vfs::exists(const std::filesystem::path& path) const
    {
        if (is_native(path))
        {
            return platform::file_exists(path);
        }
        const std::optional<std::string> relative = mount_relative(path);
        if (!relative.has_value())
        {
            return false;
        }
        {
            std::shared_lock lock{m_mutex};
            for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it)
            {
                if ((*it)->exists(*relative))
                {
                    return true;
                }
            }
        }
        return platform::file_exists(path);
    }

    bool vfs::read_file(const std::filesystem::path& path, std::vector<std::byte>& out, std::string* error) const
    {
        std::string reason;
        if (is_native(path))
        {
            const bool ok = platform::read_file(path, out, &reason);
            if (!ok && error != nullptr)
            {
                *error = reason;
            }
            return ok;
        }

        const std::optional<std::string> relative = mount_relative(path);
        if (!relative.has_value())
        {
            out.clear();
            if (error != nullptr)
            {
                *error = "path escapes the mount";
            }
            return false;
        }
        {
            std::shared_lock lock{m_mutex};
            for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it)
            {
                if ((*it)->exists(*relative))
                {
                    const bool ok = (*it)->read(*relative, out, reason);
                    if (!ok && error != nullptr)
                    {
                        *error = reason;
                    }
                    return ok;
                }
            }
        }

        // No mount holds it: the working-directory fallback.
        const bool ok = platform::read_file(path, out, &reason);
        if (!ok && error != nullptr)
        {
            *error = reason;
        }
        return ok;
    }

    bool vfs::read_text_file(const std::filesystem::path& path, std::string& out, std::string* error) const
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

    std::filesystem::path vfs::resolve(const std::filesystem::path& path) const
    {
        if (is_native(path))
        {
            return path;
        }
        const std::optional<std::string> relative = mount_relative(path);
        if (!relative.has_value())
        {
            return path;
        }
        std::shared_lock lock{m_mutex};
        for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it)
        {
            if ((*it)->exists(*relative))
            {
                std::filesystem::path native = (*it)->native_path(*relative);
                return native.empty() ? path : native;
            }
        }
        return path;
    }

    std::string vfs::canonical_key(const std::filesystem::path& path) const
    {
        const std::filesystem::path resolved = resolve(path);
        std::error_code error;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(resolved, error);
        if (error)
        {
            canonical = std::filesystem::absolute(resolved, error);
        }
        if (error)
        {
            canonical = resolved;
        }
        std::string key = canonical.lexically_normal().generic_string();
        if (platform::case_insensitive_paths())
        {
            key = platform::fold_path_case(std::move(key));
        }
        return key;
    }

    std::optional<std::filesystem::file_time_type> vfs::last_write_time(const std::filesystem::path& path) const
    {
        if (is_native(path))
        {
            return platform::last_write_time(path);
        }
        const std::optional<std::string> relative = mount_relative(path);
        if (!relative.has_value())
        {
            return std::nullopt;
        }
        {
            std::shared_lock lock{m_mutex};
            for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it)
            {
                if ((*it)->exists(*relative))
                {
                    return (*it)->last_write_time(*relative);
                }
            }
        }
        return platform::last_write_time(path);
    }

    vfs& default_vfs()
    {
        static vfs instance;
        return instance;
    }
} // namespace core
