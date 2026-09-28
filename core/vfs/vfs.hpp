// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
#include <string_view>
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

        /** @brief Whether @ref write can store files in this mount. A mount is read-only unless it says otherwise. */
        virtual bool writable() const
        {
            return false;
        }

        /**
         * @brief Writes @p size bytes from @p data to @p relative, creating the
         *        file (and the directories leading to it) or replacing it.
         * @return false with @p error set when the file cannot be written; the
         *         default refuses, for a read-only mount.
         */
        virtual bool write(const std::string& relative, const void* data, std::size_t size, std::string& error)
        {
            (void)relative;
            (void)data;
            (void)size;
            error = "the mount is read-only";
            return false;
        }
    };

    /**
     * @brief The engine's virtual filesystem: an ordered set of mount points
     *        that a relative asset path is looked up in.
     *
     * Every asset loader (`assets::image`, `assets::font`,
     * the asset cache and the glTF importer) reads its files through here, so
     * where the assets come from is decided once — the engine mounts the content root
     * (`settings.content.root`: the project's, by default) at start-up,
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
         * @brief Writes @p size bytes from @p data to the file @p path names,
         *        creating it (and any missing parent directory) or replacing it.
         *
         * An absolute path writes that native file. A relative path is written
         * where a later read finds it: into the highest-priority mount that
         * already holds the file, when that mount is writable, otherwise into
         * the highest-priority writable mount; with no writable mount it is
         * written relative to the working directory, the reads' fallback.
         * @param error When non-null, receives a one-line reason on failure.
         * @return false when the file could not be written.
         */
        bool
        write_file(const std::filesystem::path& path, const void* data, std::size_t size, std::string* error = nullptr);

        /** @brief @ref write_file of @p text. */
        bool write_text_file(const std::filesystem::path& path, std::string_view text, std::string* error = nullptr);

        /**
         * @brief The mount-relative path that reaches the native file
         *        @p native — the inverse of @ref resolve — or @c std::nullopt
         *        when no mount with native files holds it, or a
         *        higher-priority mount would shadow it under that path.
         *
         * Lets a caller holding a native path or a @ref canonical_key (an
         * asset-cache key, say) store it portably, as the path the VFS
         * resolves on another machine. Case-folded where @ref canonical_key
         * folds.
         */
        std::optional<std::string> virtual_path(const std::filesystem::path& native) const;

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

        // canonical_key without the mount lookup: weakly canonical, generic
        // separators, case-folded where paths ignore case.
        static std::string canonical_form(const std::filesystem::path& native);

        mutable std::shared_mutex m_mutex;
        std::vector<std::unique_ptr<vfs_mount>> m_mounts; // lowest priority first
    };

    /**
     * @brief The process-wide @ref vfs the asset loaders read through.
     * The engine mounts the content root on it at start-up and unmounts at
     * shutdown; a test mounts whatever directory it needs. With nothing
     * mounted every path is read natively, so code that hands the loaders
     * real paths keeps working unchanged.
     */
    vfs& default_vfs();
} // namespace core
