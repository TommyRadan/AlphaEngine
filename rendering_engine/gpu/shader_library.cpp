// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/shader_library.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/vfs/vfs.hpp>

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

        // Text read from a file, keyed by library path. Kept for the
        // process so the returned views stay valid; refresh moves an
        // entry's map node to retired (a node handle keeps the string
        // where it is), so a view taken before the file was re-read still
        // points at live text.
        using file_map = std::map<std::string, std::string, std::less<>>;

        // Asset shaders read from the VFS, with the modification time each
        // was read at (debug builds compare it in changed_asset_paths).
        struct asset_state
        {
            file_map files;
            std::map<std::string, std::optional<std::filesystem::file_time_type>, std::less<>> stamps;
            std::vector<file_map::node_type> retired;
        };

        asset_state& assets()
        {
            static asset_state state;
            return state;
        }

        // The VFS path of the asset shader @p path names.
        std::filesystem::path asset_file(std::string_view path)
        {
            std::string spelled{asset_directory};
            spelled += '/';
            spelled += path;
            return core::os::utf8_path(spelled);
        }

        // The asset text for path, or nullptr when no VFS mount holds
        // shaders/<path>. Only the mounts are searched: a relative path the
        // VFS would otherwise read from the working directory (the
        // repository's own shaders/, for a binary run from the source
        // tree) must fall through to the embedded copy. A file that exists
        // but cannot be read is logged and treated as absent.
        const std::string* asset_source(std::string_view path)
        {
            asset_state& state = assets();
            if (const auto cached = state.files.find(path); cached != state.files.end())
            {
                return &cached->second;
            }

            const core::vfs& vfs = core::default_vfs();
            const std::filesystem::path file = asset_file(path);
            if (!vfs.mounted(file))
            {
                return nullptr;
            }
            std::string text;
            std::string error;
            if (!vfs.read_text_file(file, text, &error))
            {
                LOG_WRN("Shader library: cannot read asset shader '%s' (%s); using the engine's copy",
                        core::os::path_to_utf8(file).c_str(),
                        error.c_str());
                return nullptr;
            }
            LOG_INF("Shader library: '%.*s' read from the content directory (%s)",
                    static_cast<int>(path.size()),
                    path.data(),
                    core::os::path_to_utf8(vfs.resolve(file)).c_str());
            state.stamps.insert_or_assign(std::string{path}, vfs.last_write_time(file));
            return &state.files.emplace(std::string{path}, std::move(text)).first->second;
        }

#if defined(_DEBUG)
        // On-disk override state. The root is resolved lazily on the first
        // lookup (environment variable, else the build-time source
        // directory); set_override_root replaces it. Files read from disk
        // are kept like the asset text above.
        struct override_state
        {
            bool resolved{false};
            std::filesystem::path root;
            bool announced{false};
            file_map files;
            std::vector<file_map::node_type> retired;
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
        if (const std::string* text = asset_source(path); text != nullptr)
        {
            return *text;
        }
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
        if (asset_source(path) != nullptr)
        {
            return true;
        }
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
        state.retired.clear();
#else
        (void)root;
#endif
    }

    void refresh(std::string_view path)
    {
#if defined(_DEBUG)
        override_state& state = overrides();
        if (const auto cached = state.files.find(path); cached != state.files.end())
        {
            state.retired.push_back(state.files.extract(cached));
        }
        asset_state& asset = assets();
        if (const auto cached = asset.files.find(path); cached != asset.files.end())
        {
            asset.retired.push_back(asset.files.extract(cached));
        }
        if (const auto stamp = asset.stamps.find(path); stamp != asset.stamps.end())
        {
            asset.stamps.erase(stamp);
        }
#else
        (void)path;
#endif
    }

    std::vector<std::string> changed_asset_paths()
    {
        std::vector<std::string> changed;
#if defined(_DEBUG)
        const core::vfs& vfs = core::default_vfs();
        for (const auto& [path, stamp] : assets().stamps)
        {
            const std::filesystem::path file = asset_file(path);
            if (!vfs.mounted(file) || vfs.last_write_time(file) != stamp)
            {
                changed.push_back(path);
            }
        }
#endif
        return changed;
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
