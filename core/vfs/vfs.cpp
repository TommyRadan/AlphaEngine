// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/vfs/vfs.hpp>

#include <mutex>
#include <system_error>
#include <utility>

#include <core/log.hpp>
#include <core/os/os.hpp>
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
                    os::path_to_utf8(root).c_str());
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
            return os::file_exists(path);
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
        return os::file_exists(path);
    }

    bool vfs::read_file(const std::filesystem::path& path, std::vector<std::byte>& out, std::string* error) const
    {
        std::string reason;
        if (is_native(path))
        {
            const bool ok = os::read_file(path, out, &reason);
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
        const bool ok = os::read_file(path, out, &reason);
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
        return canonical_form(resolve(path));
    }

    std::string vfs::canonical_form(const std::filesystem::path& native)
    {
        std::error_code error;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(native, error);
        if (error)
        {
            canonical = std::filesystem::absolute(native, error);
        }
        if (error)
        {
            canonical = native;
        }
        std::string key = canonical.lexically_normal().generic_string();
        if (os::case_insensitive_paths())
        {
            key = os::fold_path_case(std::move(key));
        }
        return key;
    }

    bool vfs::write_file(const std::filesystem::path& path, const void* data, std::size_t size, std::string* error)
    {
        std::string reason;
        auto finish = [&](bool ok)
        {
            if (!ok && error != nullptr)
            {
                *error = reason;
            }
            return ok;
        };

        // A native target (absolute, or the working-directory fallback) gets
        // its missing parent directories, as a mount's does.
        auto write_native = [&](const std::filesystem::path& target)
        {
            std::error_code ignored;
            if (target.has_parent_path())
            {
                std::filesystem::create_directories(target.parent_path(), ignored);
            }
            return os::write_file(target, data, size, &reason);
        };

        if (is_native(path))
        {
            return finish(write_native(path));
        }

        const std::optional<std::string> relative = mount_relative(path);
        if (!relative.has_value())
        {
            reason = "path escapes the mount";
            return finish(false);
        }
        {
            std::shared_lock lock{m_mutex};
            vfs_mount* target = nullptr;
            for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it)
            {
                if ((*it)->exists(*relative) && (*it)->writable())
                {
                    target = it->get();
                    break;
                }
            }
            if (target == nullptr)
            {
                for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it)
                {
                    if ((*it)->writable())
                    {
                        target = it->get();
                        break;
                    }
                }
            }
            if (target != nullptr)
            {
                return finish(target->write(*relative, data, size, reason));
            }
        }

        // No writable mount: the working-directory fallback reads use.
        return finish(write_native(path));
    }

    bool vfs::write_text_file(const std::filesystem::path& path, std::string_view text, std::string* error)
    {
        return write_file(path, text.data(), text.size(), error);
    }

    std::optional<std::string> vfs::virtual_path(const std::filesystem::path& native) const
    {
        const std::string target = canonical_form(native);
        std::shared_lock lock{m_mutex};
        for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it)
        {
            const std::filesystem::path root = (*it)->native_path(std::string{});
            if (root.empty())
            {
                continue;
            }
            std::string prefix = canonical_form(root);
            while (!prefix.empty() && prefix.back() == '/')
            {
                prefix.pop_back();
            }
            if (target.size() <= prefix.size() + 1 || target.compare(0, prefix.size(), prefix) != 0 ||
                target[prefix.size()] != '/')
            {
                continue;
            }
            std::string relative = target.substr(prefix.size() + 1);
            // Under that path a read looks in the higher mounts first.
            for (auto higher = m_mounts.rbegin(); higher != it; ++higher)
            {
                if ((*higher)->exists(relative))
                {
                    return std::nullopt;
                }
            }
            return relative;
        }
        return std::nullopt;
    }

    std::optional<std::filesystem::file_time_type> vfs::last_write_time(const std::filesystem::path& path) const
    {
        if (is_native(path))
        {
            return os::last_write_time(path);
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
        return os::last_write_time(path);
    }

    vfs& default_vfs()
    {
        static vfs instance;
        return instance;
    }
} // namespace core
