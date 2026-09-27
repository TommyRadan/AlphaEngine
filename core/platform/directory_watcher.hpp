// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file directory_watcher.hpp
 * @brief Change detection over a directory tree, by polling.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace core::platform
{
    /** @brief One change @ref directory_watcher::poll observed. */
    struct file_change
    {
        enum class kind
        {
            added,    /**< A regular file appeared. */
            modified, /**< Its size or modification time changed. */
            removed,  /**< It is gone. */
        };

        kind change{kind::modified};

        // The file, as `<root>/<relative>` — the same spelling the tree walk
        // produced, so it resolves directly.
        std::filesystem::path path;
    };

    /**
     * @brief Reports files added, modified or removed under a directory
     *        between two calls to @ref poll.
     *
     * A polling implementation: each @ref poll walks the tree and compares
     * every regular file's size and modification time with the snapshot the
     * previous call took. That is portable and dependency-free at the cost of
     * a directory scan per poll, so callers poll at a modest cadence (the
     * asset cache's debug hot reload polls about once a second); an OS-notified
     * implementation can replace the body without changing the interface.
     *
     * The constructor takes the baseline snapshot, so the first @ref poll
     * reports only changes made after construction. A root that does not
     * exist (or vanishes) is treated as empty: every tracked file is reported
     * removed and later polls report nothing until it reappears.
     *
     * Not thread-safe: poll from one thread.
     */
    struct directory_watcher
    {
        /**
         * @param root      Directory to watch.
         * @param recursive Whether to descend into subdirectories.
         */
        explicit directory_watcher(std::filesystem::path root, bool recursive = true);

        /** @brief The watched directory. */
        const std::filesystem::path& root() const noexcept;

        /**
         * @brief Rescans the tree and returns every change since the previous
         *        call (or since construction), in no particular order.
         */
        std::vector<file_change> poll();

        /** @brief Number of regular files in the last snapshot. */
        std::size_t tracked_count() const noexcept;

    private:
        struct file_stamp
        {
            std::filesystem::file_time_type modified;
            std::uintmax_t size{0};
        };

        // One snapshot of the tree: every regular file, keyed by its generic
        // path string (stable across the two directory-iterator flavours).
        using snapshot = std::unordered_map<std::string, file_stamp>;

        snapshot scan() const;

        std::filesystem::path m_root;
        bool m_recursive;
        snapshot m_last;
    };
} // namespace core::platform
