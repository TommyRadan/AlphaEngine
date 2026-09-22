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

#include <rendering_engine/gpu/shader_library.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

#include <core/log.hpp>

namespace rendering_engine::gpu::shader_library
{
    namespace
    {
        struct embedded_entry
        {
            std::string_view path;
            std::string_view source;
        };

        // The registry, sorted by path once so lookups binary-search.
        const std::vector<embedded_entry>& embedded_index()
        {
            static const std::vector<embedded_entry> index = []
            {
                std::size_t count = 0;
                const detail::shader_registry_entry* entries = detail::embedded_shaders(count);
                std::vector<embedded_entry> result;
                result.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    result.push_back(
                        {std::string_view{entries[i].path}, std::string_view{entries[i].source, entries[i].size}});
                }
                std::sort(result.begin(),
                          result.end(),
                          [](const embedded_entry& a, const embedded_entry& b) { return a.path < b.path; });
                return result;
            }();
            return index;
        }

        const embedded_entry* find_embedded(std::string_view path)
        {
            const auto& index = embedded_index();
            const auto it =
                std::lower_bound(index.begin(),
                                 index.end(),
                                 path,
                                 [](const embedded_entry& entry, std::string_view key) { return entry.path < key; });
            if (it == index.end() || it->path != path)
            {
                return nullptr;
            }
            return &*it;
        }

#if defined(_DEBUG)
        // On-disk override state. The root is resolved lazily on the first
        // lookup (environment variable, else the build-time source
        // directory); set_override_root replaces it. Files read from disk
        // are kept for the process so the returned views stay valid.
        struct override_state
        {
            bool resolved{false};
            std::filesystem::path root;
            bool announced{false};
            std::map<std::string, std::string, std::less<>> files;
        };

        override_state& overrides()
        {
            static override_state state;
            return state;
        }

        bool disables_override(const char* value)
        {
            return value[0] == '\0' || std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0 ||
                   std::strcmp(value, "false") == 0;
        }

        void resolve_override_root()
        {
            override_state& state = overrides();
            if (state.resolved)
            {
                return;
            }
            state.resolved = true;

            if (const char* value = std::getenv("ALPHAENGINE_SHADER_DIR"); value != nullptr)
            {
                if (!disables_override(value))
                {
                    state.root = std::filesystem::path{value};
                }
                return;
            }
#if defined(ALPHAENGINE_SHADER_SOURCE_DIR)
            state.root = std::filesystem::path{ALPHAENGINE_SHADER_SOURCE_DIR};
#endif
        }

        // The on-disk text for path, or nullptr when the override root is
        // off or has no such file. A file that exists but cannot be read
        // is logged once and treated as absent.
        const std::string* override_source(std::string_view path)
        {
            resolve_override_root();
            override_state& state = overrides();
            if (state.root.empty())
            {
                return nullptr;
            }

            if (const auto cached = state.files.find(path); cached != state.files.end())
            {
                return &cached->second;
            }

            const std::filesystem::path file = state.root / std::filesystem::path{std::string{path}};
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error))
            {
                return nullptr;
            }

            std::ifstream in{file, std::ios::binary};
            if (!in)
            {
                LOG_WRN("Shader library: cannot read override '%s'; using the embedded copy", file.string().c_str());
                return nullptr;
            }
            std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

            if (!state.announced)
            {
                state.announced = true;
                LOG_INF("Shader library: reading shader overrides from %s", state.root.string().c_str());
            }
            return &state.files.emplace(std::string{path}, std::move(text)).first->second;
        }
#endif
    } // namespace

    std::string_view source(std::string_view path)
    {
#if defined(_DEBUG)
        if (const std::string* text = override_source(path); text != nullptr)
        {
            return *text;
        }
#endif
        if (const embedded_entry* entry = find_embedded(path); entry != nullptr)
        {
            return entry->source;
        }
        throw std::runtime_error{"shader_library: no shader named '" + std::string{path} + "'"};
    }

    bool contains(std::string_view path)
    {
#if defined(_DEBUG)
        if (override_source(path) != nullptr)
        {
            return true;
        }
#endif
        return find_embedded(path) != nullptr;
    }

    std::vector<std::string_view> embedded_paths()
    {
        std::vector<std::string_view> paths;
        const auto& index = embedded_index();
        paths.reserve(index.size());
        for (const embedded_entry& entry : index)
        {
            paths.push_back(entry.path);
        }
        return paths;
    }

    void set_override_root(const std::filesystem::path& root)
    {
#if defined(_DEBUG)
        override_state& state = overrides();
        state.resolved = true;
        state.root = root;
        state.announced = false;
        state.files.clear();
#else
        (void)root;
#endif
    }

    const std::filesystem::path& override_root()
    {
#if defined(_DEBUG)
        resolve_override_root();
        return overrides().root;
#else
        static const std::filesystem::path none;
        return none;
#endif
    }
} // namespace rendering_engine::gpu::shader_library
