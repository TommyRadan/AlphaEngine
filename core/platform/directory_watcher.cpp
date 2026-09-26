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

#include <core/platform/directory_watcher.hpp>

#include <system_error>
#include <utility>

namespace core::platform
{
    directory_watcher::directory_watcher(std::filesystem::path root, bool recursive)
        : m_root{std::move(root)}, m_recursive{recursive}, m_last{scan()}
    {
    }

    const std::filesystem::path& directory_watcher::root() const noexcept
    {
        return m_root;
    }

    std::size_t directory_watcher::tracked_count() const noexcept
    {
        return m_last.size();
    }

    directory_watcher::snapshot directory_watcher::scan() const
    {
        snapshot result;
        std::error_code error;
        if (!std::filesystem::is_directory(m_root, error))
        {
            return result;
        }

        // The two iterator flavours share no base type, so the per-entry work
        // is a lambda both loops call. Every filesystem query takes an
        // error_code: a file that disappears mid-scan, or one we cannot stat,
        // is simply left out of the snapshot rather than throwing.
        const auto record = [&result](const std::filesystem::directory_entry& entry)
        {
            std::error_code entry_error;
            if (!entry.is_regular_file(entry_error) || entry_error)
            {
                return;
            }
            file_stamp stamp;
            stamp.modified = entry.last_write_time(entry_error);
            if (entry_error)
            {
                return;
            }
            stamp.size = entry.file_size(entry_error);
            if (entry_error)
            {
                return;
            }
            result.emplace(entry.path().generic_string(), stamp);
        };

        constexpr auto k_options = std::filesystem::directory_options::skip_permission_denied;
        if (m_recursive)
        {
            for (std::filesystem::recursive_directory_iterator it{m_root, k_options, error}, end; !error && it != end;
                 it.increment(error))
            {
                record(*it);
            }
        }
        else
        {
            for (std::filesystem::directory_iterator it{m_root, k_options, error}, end; !error && it != end;
                 it.increment(error))
            {
                record(*it);
            }
        }
        return result;
    }

    std::vector<file_change> directory_watcher::poll()
    {
        snapshot current = scan();
        std::vector<file_change> changes;

        for (const auto& [path, stamp] : current)
        {
            const auto previous = m_last.find(path);
            if (previous == m_last.end())
            {
                changes.push_back(file_change{file_change::kind::added, std::filesystem::path{path}});
            }
            else if (previous->second.modified != stamp.modified || previous->second.size != stamp.size)
            {
                changes.push_back(file_change{file_change::kind::modified, std::filesystem::path{path}});
            }
        }
        for (const auto& [path, stamp] : m_last)
        {
            if (current.find(path) == current.end())
            {
                changes.push_back(file_change{file_change::kind::removed, std::filesystem::path{path}});
            }
        }

        m_last = std::move(current);
        return changes;
    }
} // namespace core::platform
