// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file resource_store.hpp
 * @brief The per-frame resource store passes publish into and look up in,
 *        and the declarations the pass list validates under the same names.
 *
 * A pass reaches what another pass produced only through a
 * @ref resource_store: the producer publishes a value under a
 * @ref resource_key, the consumer looks the key up, and neither knows the
 * other's type. The names the passes declare through
 * @ref pass_io_builder are the keys' names, so a declared dependency and
 * the value that travels along it are one thing: the pass list validates
 * the order of those names, and a debug build checks every publish and
 * lookup a pass makes against what it declared.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rendering_engine
{
    /**
     * @brief Names one resource of a @ref resource_store and the type of
     *        the value it holds.
     *
     * Declared once, as a constant, and shared by the pass that publishes
     * the resource, every pass that looks it up and the declarations of
     * both (frame_resources.hpp holds the renderer's and the built-in
     * passes' keys). The value is anything trivially copyable: a GPU
     * handle, a matrix, a small struct of them, or a pointer to an
     * interface its producer implements.
     */
    template<typename T>
    struct resource_key
    {
        static_assert(std::is_trivially_copyable_v<T>, "a resource value is a plain, trivially copyable value");
        static_assert(alignof(T) <= alignof(std::max_align_t), "a resource value needs at most fundamental alignment");

        std::string_view name;
    };

    /// How a pass uses a resource it declares (see @ref pass_io_builder).
    enum class resource_access : uint8_t
    {
        // Needed: an earlier pass writes it, or the renderer imports it.
        read,
        // Used when present: the pass falls back when nothing publishes it
        // this frame, but a pass that writes it must run earlier.
        read_optional,
        // Written at any position: the previous frame's contents (a
        // history the pass carries over), or a value a later pass
        // publishes while it prepares, looked up while this pass records.
        read_unordered,
        // Produced, or drawn into, by the pass.
        write,
    };

    /// One resource a pass declares, and how it uses it.
    struct resource_use
    {
        std::string name;
        resource_access access{resource_access::read};
    };

    /**
     * @brief Collects one pass's declared resource reads and writes.
     *
     * A pass names the resources it uses by their store keys (or, for one
     * that never travels through the store, such as a history the pass
     * keeps to itself, by a plain name), so it never has to thread handles
     * through its interface. The renderer's @ref pass_list validates the
     * order once per change to the list; a pass that declares nothing is
     * an ordering-only entry, recorded in place but invisible to the
     * check.
     */
    class pass_io_builder
    {
    public:
        /// A resource the pass cannot do without (@ref resource_access::read).
        void read(std::string_view resource)
        {
            add(resource, resource_access::read);
        }

        /// A resource the pass uses when present (@ref resource_access::read_optional).
        void read_optional(std::string_view resource)
        {
            add(resource, resource_access::read_optional);
        }

        /// A resource written at any position (@ref resource_access::read_unordered).
        void read_unordered(std::string_view resource)
        {
            add(resource, resource_access::read_unordered);
        }

        /// A resource the pass produces or draws into.
        void write(std::string_view resource)
        {
            add(resource, resource_access::write);
        }

        template<typename T>
        void read(const resource_key<T>& key)
        {
            read(key.name);
        }

        template<typename T>
        void read_optional(const resource_key<T>& key)
        {
            read_optional(key.name);
        }

        template<typename T>
        void read_unordered(const resource_key<T>& key)
        {
            read_unordered(key.name);
        }

        template<typename T>
        void write(const resource_key<T>& key)
        {
            write(key.name);
        }

        /// Every declared use, in declaration order.
        const std::vector<resource_use>& uses() const noexcept
        {
            return m_uses;
        }

        /// Whether the pass declared @p resource at all (any read, or a write).
        bool declares(std::string_view resource) const noexcept;

        /// Whether the pass declared @p resource as a write.
        bool declares_write(std::string_view resource) const noexcept;

    private:
        void add(std::string_view resource, resource_access access)
        {
            m_uses.push_back({std::string{resource}, access});
        }

        std::vector<resource_use> m_uses;
    };

    /**
     * @brief The frame's named resources: what the renderer and the passes
     *        publish for the passes after them.
     *
     * The renderer owns the store and hands it to every pass through
     * @c frame_context::resources. At the top of each frame it clears the
     * store and publishes what it owns (the swapchain, the off-screen
     * targets, the colour-grading table); then every pass publishes what
     * it produces from its @c pass::prepare, in list order. A name holds
     * one value at a time: a pass that republishes a name (motion blur
     * handing on its blurred copy of the scene colour) gives the passes
     * after it a new version, while the passes before it keep the one they
     * looked up. So a pass looks up, in its own @c prepare, what the
     * passes before it produced and keeps what its @c record needs; once
     * every pass has prepared, the store no longer changes, and a lookup
     * from @c record sees each name's last value — which is how a pass
     * reaches something a later pass prepares (the depth pre-pass, the
     * scene pass's draw list).
     *
     * Values are copied in and live until the next @ref clear; a lookup
     * returns a pointer that stays valid until then. Nothing is allocated
     * per frame once each name has been published once. Main-thread only.
     */
    class resource_store
    {
    public:
        resource_store() = default;
        ~resource_store() = default;

        resource_store(const resource_store&) = delete;
        resource_store& operator=(const resource_store&) = delete;

        /**
         * @brief Publishes @p value under @p key until the next @ref clear,
         *        replacing what the name held.
         *
         * A name keeps the type it was first published with; publishing
         * another type under it is a programming error, which asserts in
         * debug builds and is logged and ignored otherwise.
         */
        template<typename T>
        void publish(const resource_key<T>& key, const T& value)
        {
            if (void* storage = publish_slot(key.name, typeid(T), sizeof(T)); storage != nullptr)
            {
                ::new (storage) T(value);
            }
        }

        /// The value published under @p key since the last @ref clear, or null when none was.
        template<typename T>
        const T* find(const resource_key<T>& key) const
        {
            const void* storage = find_slot(key.name, typeid(T));
            return storage != nullptr ? std::launder(static_cast<const T*>(storage)) : nullptr;
        }

        /// The value published under @p key, or a value-initialised @c T (an invalid handle) when none was.
        template<typename T>
        T get(const resource_key<T>& key) const
        {
            const T* value = find(key);
            return value != nullptr ? *value : T{};
        }

        /**
         * @brief Forgets every published value, keeping the storage.
         *
         * The renderer clears the store at the top of every frame, and
         * whenever what it holds may have gone stale between frames (a
         * resize recreated the targets, a pass left the list).
         */
        void clear() noexcept;

        /**
         * @brief Names the pass whose publishes and lookups follow, and what
         *        it declared, or null for none.
         *
         * The pass list sets it around every pass's @c prepare and
         * @c record. In debug builds a publish under a name the pass did
         * not declare as a write, or a lookup of a name it did not declare
         * at all, is reported as an error, once per pass and name, so a
         * declaration cannot drift from what the pass does. The renderer's
         * own publishes run with none set. Release builds keep nothing.
         */
        void set_declared_access(const char* pass_name, const pass_io_builder* declared) noexcept
        {
#if _DEBUG
            m_pass_name = pass_name;
            m_declared = declared;
#else
            (void)pass_name;
            (void)declared;
#endif
        }

    private:
        // One name's value: its type (fixed by the first publish), its
        // storage, and the clear generation it was last published in.
        struct slot
        {
            std::type_index type;
            std::size_t size{0};
            std::unique_ptr<std::byte[]> storage;
            uint64_t generation{0};
        };

        // Heterogeneous lookup, so a lookup by key allocates nothing.
        struct name_hash
        {
            using is_transparent = void;

            std::size_t operator()(std::string_view name) const noexcept
            {
                return std::hash<std::string_view>{}(name);
            }
        };

        // Storage for publishing @p name as a @p type of @p size bytes,
        // stamped as published this generation; null on a type mismatch.
        void* publish_slot(std::string_view name, const std::type_info& type, std::size_t size);

        // Storage of @p name when it was published this generation as a
        // @p type; null otherwise.
        const void* find_slot(std::string_view name, const std::type_info& type) const;

#if _DEBUG
        // Reports, once per pass and name, an access the current pass did
        // not declare.
        void check_access(std::string_view name, bool publishing) const;

        const char* m_pass_name{nullptr};
        const pass_io_builder* m_declared{nullptr};
        mutable std::unordered_set<std::string> m_reported;
#endif

        std::unordered_map<std::string, slot, name_hash, std::equal_to<>> m_slots;

        // Bumped by clear(): a slot holds a value only while its
        // generation matches.
        uint64_t m_generation{1};
    };
} // namespace rendering_engine
