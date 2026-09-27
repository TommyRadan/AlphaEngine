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
