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
