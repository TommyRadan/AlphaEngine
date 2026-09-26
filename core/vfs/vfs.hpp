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
 * @file vfs.hpp
 * @brief The virtual filesystem: mount points the asset loaders read through.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace core
{
    /**
     * @brief One mount point of the @ref vfs: a source of files addressed by
     *        a mount-relative path (`textures/wood.png`).
     *
     * A loose directory today (@ref directory_mount); a pak / archive mount
     * implements the same interface and the loaders never notice. Relative
     * paths arrive normalised (`lexically_normal`, generic separators, never
     * escaping the mount with `..`).
     *
     * Implementations must be safe to read from several threads at once —
     * asynchronous asset loads read on the worker pool.
     */
    struct vfs_mount
    {
        virtual ~vfs_mount() = default;

        /** @brief A one-line description for the log (`directory /path/to/assets`). */
        virtual std::string describe() const = 0;

        /** @brief Whether the mount holds a file at @p relative. */
        virtual bool exists(const std::string& relative) const = 0;

        /**
         * @brief Reads the whole file at @p relative into @p out.
         * @return false with @p error set when the mount holds no such file or it cannot be read.
         */
        virtual bool read(const std::string& relative, std::vector<std::byte>& out, std::string& error) const = 0;

        /**
         * @brief The native filesystem path of @p relative when the mount is
         *        backed by real files (a directory), else an empty path (an
         *        archive). Callers that need a native path — for a cache key
         *        or an OS file API — go through @ref vfs::resolve.
         */
        virtual std::filesystem::path native_path(const std::string& relative) const = 0;

        /** @brief The last modification time of @p relative, or @c std::nullopt when unknown. */
        virtual std::optional<std::filesystem::file_time_type> last_write_time(const std::string& relative) const = 0;
    };

    /**
     * @brief The engine's virtual filesystem: an ordered set of mount points
     *        that a relative asset path is looked up in.
     *
     * Every asset loader (`util::image`, `util::font`, the asset cache and
     * the glTF importer) reads its files through here, so where the assets
     * come from is decided once — the engine mounts the asset root
     * (`core::platform::asset_root()`, or `settings.assets.root`) at start-up,
     * a test mounts a scratch directory — and the loaders stay oblivious.
     *
     * Path rules:
     * - An **absolute** (or root-relative) path bypasses the mounts and names
     *   the native file directly, so a caller with a real path in hand (a
     *   command-line argument, a temp file in a test) can still use it.
     * - A **relative** path is looked up in the mounts, most recently mounted
     *   first, so a later mount shadows an earlier one — how a patch or mod
     *   directory overrides the base assets. A path that no mount holds is
     *   read as it is, relative to the working directory, which keeps the
     *   pre-VFS behaviour working and lets the failure name the file asked
     *   for.
     * - `..` components are normalised away; a relative path that would
     *   escape its mount is treated as not found.
     *
     * Reads are thread-safe (a shared lock over the mount list); mounting and
     * unmounting take the exclusive lock and belong to the main thread at
     * start-up and shutdown.
     */
    struct vfs
    {
        vfs() = default;
        vfs(const vfs&) = delete;
        vfs& operator=(const vfs&) = delete;

        /** @brief Adds @p mount as the highest-priority mount. */
        void mount(std::unique_ptr<vfs_mount> mount);

        /** @brief Mounts the loose directory @p root (it need not exist yet; a missing root is logged). */
        void mount_directory(const std::filesystem::path& root);

        /** @brief Removes every mount. */
        void unmount_all();

        /** @brief Number of mounts. */
        std::size_t mount_count() const;

        /** @brief Whether @p path names a file: a native one when absolute, one in some mount otherwise. */
        bool exists(const std::filesystem::path& path) const;

        /**
         * @brief Reads the whole file @p path names into @p out.
         * @param error When non-null, receives a one-line reason on failure.
         * @return false when no file was found or it could not be read; @p out is then empty.
         */
        bool
        read_file(const std::filesystem::path& path, std::vector<std::byte>& out, std::string* error = nullptr) const;

        /** @brief @ref read_file into a string. */
        bool read_text_file(const std::filesystem::path& path, std::string& out, std::string* error = nullptr) const;

        /**
         * @brief The native path @p path resolves to: itself when absolute,
         *        the file in the first mount that holds it otherwise, or
         *        @p path unchanged when no mount does (or the holding mount
         *        has no native files).
         */
        std::filesystem::path resolve(const std::filesystem::path& path) const;

        /**
         * @brief A stable identity for the file @p path names, for cache keys.
         * @ref resolve, then `weakly_canonical` (symlinks and `..` collapsed,
         * made absolute), generic separators, and — on platforms whose
         * filesystems ignore case — folded to lower case. Two spellings of
         * one file (`a/./b.png`, `a/b.png`, `../x/a/b.png`, a mount-relative
         * path and its absolute native path) produce the same key.
         */
        std::string canonical_key(const std::filesystem::path& path) const;

        /** @brief The last modification time of the file @p path names, or @c std::nullopt. */
        std::optional<std::filesystem::file_time_type> last_write_time(const std::filesystem::path& path) const;

    private:
        // Whether @p path names a native file directly rather than a mount entry.
        static bool is_native(const std::filesystem::path& path);

        // The normalised mount-relative spelling of a relative @p path, or
        // std::nullopt when it would escape the mount.
        static std::optional<std::string> mount_relative(const std::filesystem::path& path);

        mutable std::shared_mutex m_mutex;
        std::vector<std::unique_ptr<vfs_mount>> m_mounts; // lowest priority first
    };

    /**
     * @brief The process-wide @ref vfs the asset loaders read through.
     * The engine mounts the asset root on it at start-up and unmounts at
     * shutdown; a test mounts whatever directory it needs. With nothing
     * mounted every path is read natively, so code that hands the loaders
     * real paths keeps working unchanged.
     */
    vfs& default_vfs();
} // namespace core
