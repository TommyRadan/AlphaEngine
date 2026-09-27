// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file directory_mount.hpp
 * @brief A @ref core::vfs_mount over a loose directory on the native filesystem.
 */

#pragma once

#include <filesystem>

#include <core/vfs/vfs.hpp>

namespace core
{
    /**
     * @brief Serves the files under one directory: `textures/wood.png`
     *        resolves to `<root>/textures/wood.png`.
     * The root need not exist when mounted (it may be created later, or be
     * a missing optional overlay); until it does every lookup misses.
     */
    struct directory_mount final : vfs_mount
    {
        explicit directory_mount(std::filesystem::path root);

        /** @brief The mounted directory. */
        const std::filesystem::path& root() const noexcept;

        std::string describe() const override;
        bool exists(const std::string& relative) const override;
        bool read(const std::string& relative, std::vector<std::byte>& out, std::string& error) const override;
        std::filesystem::path native_path(const std::string& relative) const override;
        std::optional<std::filesystem::file_time_type> last_write_time(const std::string& relative) const override;

        /** @brief A directory is writable: files are written under the root, which is created on demand. */
        bool writable() const override;
        bool write(const std::string& relative, const void* data, std::size_t size, std::string& error) override;

    private:
        std::filesystem::path m_root;
    };
} // namespace core
