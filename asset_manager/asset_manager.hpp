/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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
 * @file asset_manager.hpp
 * @brief Handle-based asset cache keyed by filesystem path.
 */

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include <infrastructure/log.hpp>
#include <infrastructure/singleton.hpp>

namespace asset_manager
{
    /**
     * @brief Process-wide asset cache that deduplicates assets by path.
     *
     * Assets are returned as @c std::shared_ptr<T> so their lifetime is tied
     * to their usage. Internally the cache stores @c std::weak_ptr entries,
     * so once every handle to a given asset is dropped the underlying object
     * is destroyed immediately; @ref collect_unused simply prunes the
     * expired bookkeeping entries from the lookup tables.
     *
     * The manager is keyed by @c (type, path) — the same path loaded as two
     * different asset types is tracked as two independent entries. The path
     * is stored using @c std::filesystem::path::generic_string() so that
     * `a/b.png` and `a\b.png` hit the same cache slot on Windows.
     *
     * Listener storage and access are not thread-safe — register and load
     * from the main loop thread only.
     */
    struct context : public singleton<context>
    {
        context() = default;

        /** @brief Initializes the asset manager. Must be called once at startup. */
        void init();

        /** @brief Shuts down the asset manager and clears all cache entries. */
        void quit();

        /**
         * @brief Returns a cached asset, loading it if not already present.
         *
         * If an entry for @p path (under type @c T) is already cached and
         * still has live handles, the existing @c shared_ptr is returned
         * and @p args are ignored. Otherwise a new @c T is constructed via
         * @c std::make_shared<T>(path_as_string, args...) and inserted
         * into the cache.
         *
         * @tparam T    Asset type. Must be constructible from
         *              @c (const std::string&, Args&&...).
         * @tparam Args Forwarded constructor arguments used on cache miss.
         * @param path  Filesystem path used as the cache key and passed to
         *              the @p T constructor.
         * @param args  Additional constructor arguments (e.g. font size).
         * @return Shared handle to the cached asset.
         */
        template<typename T, typename... Args>
        std::shared_ptr<T> load(const std::filesystem::path& path, Args&&... args)
        {
            const std::string key = path.generic_string();
            auto& bucket = m_caches[std::type_index(typeid(T))];

            auto it = bucket.find(key);
            if (it != bucket.end())
            {
                if (auto existing = std::static_pointer_cast<T>(it->second.lock()))
                {
                    return existing;
                }
                // Cached entry expired between frames — fall through and reload.
                bucket.erase(it);
            }

            auto asset = std::make_shared<T>(key, std::forward<Args>(args)...);
            bucket.emplace(key, std::weak_ptr<void>(asset));
            LOG_INF("Asset loaded (type=%s, path=\"%s\")", typeid(T).name(), key.c_str());
            return asset;
        }

        /**
         * @brief Inserts an already-constructed asset into the cache.
         *
         * Useful for asset types that do not have a path-based constructor
         * (for example, meshes that are built programmatically or loaded
         * by a caller-supplied parser). If an entry for @p path already
         * exists and is still live, the existing handle is returned and
         * @p asset is discarded.
         *
         * @tparam T    Asset type.
         * @param path  Filesystem path used as the cache key.
         * @param asset Handle to take ownership of.
         * @return The cached handle (either @p asset or the pre-existing one).
         */
        template<typename T>
        std::shared_ptr<T> insert(const std::filesystem::path& path, std::shared_ptr<T> asset)
        {
            const std::string key = path.generic_string();
            auto& bucket = m_caches[std::type_index(typeid(T))];

            auto it = bucket.find(key);
            if (it != bucket.end())
            {
                if (auto existing = std::static_pointer_cast<T>(it->second.lock()))
                {
                    return existing;
                }
                bucket.erase(it);
            }

            bucket.emplace(key, std::weak_ptr<void>(asset));
            LOG_INF("Asset inserted (type=%s, path=\"%s\")", typeid(T).name(), key.c_str());
            return asset;
        }

        /**
         * @brief Returns the cached handle for @p path, or @c nullptr if absent.
         *
         * Does not load on miss. Stale entries (weak_ptr expired) are treated
         * as absent and pruned.
         *
         * @tparam T    Asset type.
         * @param path  Filesystem path used as the cache key.
         */
        template<typename T>
        std::shared_ptr<T> get(const std::filesystem::path& path)
        {
            const std::string key = path.generic_string();
            auto type_it = m_caches.find(std::type_index(typeid(T)));
            if (type_it == m_caches.end())
            {
                return nullptr;
            }

            auto& bucket = type_it->second;
            auto it = bucket.find(key);
            if (it == bucket.end())
            {
                return nullptr;
            }

            auto existing = std::static_pointer_cast<T>(it->second.lock());
            if (!existing)
            {
                bucket.erase(it);
            }
            return existing;
        }

        /**
         * @brief Drops bookkeeping entries for assets with no live handles.
         *
         * Walks every cache bucket and removes any @c weak_ptr whose
         * referent has already been destroyed. The underlying asset is
         * destroyed as soon as the last handle drops — this call only
         * reclaims the map entries themselves.
         *
         * @return Number of entries pruned.
         */
        std::size_t collect_unused();

    private:
        std::unordered_map<std::type_index, std::unordered_map<std::string, std::weak_ptr<void>>> m_caches;
    };
} // namespace asset_manager
