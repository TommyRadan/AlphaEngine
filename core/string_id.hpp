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
 * @file string_id.hpp
 * @brief Interned string with a 64-bit FNV-1a identity, for names that are
 *        compared and looked up far more often than they are built.
 *
 * Constructing a @ref core::string_id hashes the text with
 * @ref core::fnv1a_64 and interns it in a process-wide table, so every id
 * built from the same text shares one stored copy. From then on equality and
 * hashing are a single integer compare, and @ref core::string_id::c_str /
 * @ref core::string_id::view hand back the interned text (for logs and tools)
 * without owning anything. Scene-graph node names are the first user: a scene
 * keeps a name index keyed by these ids, so a lookup by name is a hash probe
 * rather than a tree walk comparing strings.
 *
 * The interned text lives for the rest of the process, so ids are cheap to
 * copy and never dangle. Construction takes a lock on the table, so ids may
 * be built from worker threads too; reading one never locks. Two different
 * strings hashing to the same 64-bit value is detected when the second is
 * interned and logged (the later text then compares equal to the earlier
 * one), which at 64 bits is not expected outside adversarial input.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <core/hash.hpp>

namespace core
{
    /**
     * @brief An interned, hashed string: O(1) to compare, hash and copy.
     *
     * Implicitly constructible from @c const @c char*, @c std::string_view
     * and @c std::string so it can stand in for a string parameter
     * (@c node.find("sun")). A default-constructed id is the empty string and
     * interns nothing.
     */
    struct string_id
    {
        /** @brief The empty string. */
        constexpr string_id() noexcept = default;

        /** @brief Hashes and interns @p text; a null pointer is the empty string. */
        string_id(const char* text);

        /** @brief Hashes and interns @p text. */
        string_id(std::string_view text);

        /** @brief Hashes and interns @p text. */
        string_id(const std::string& text);

        /** @brief FNV-1a 64-bit hash of the text; equal ids have equal values. */
        constexpr uint64_t value() const noexcept
        {
            return m_hash;
        }

        /** @brief The interned text, null-terminated, valid for the rest of the process. */
        constexpr const char* c_str() const noexcept
        {
            return m_text;
        }

        /** @brief The interned text as a view. */
        constexpr std::string_view view() const noexcept
        {
            return std::string_view{m_text, m_size};
        }

        /** @brief Length of the text in bytes. */
        constexpr std::size_t size() const noexcept
        {
            return m_size;
        }

        /** @brief True for the empty string. */
        constexpr bool empty() const noexcept
        {
            return m_size == 0;
        }

        /** @brief Equality is identity: one integer compare. */
        friend constexpr bool operator==(const string_id& lhs, const string_id& rhs) noexcept
        {
            return lhs.m_hash == rhs.m_hash;
        }

    private:
        uint64_t m_hash{fnv1a_64_offset_basis};
        const char* m_text{""};
        std::size_t m_size{0};
    };
} // namespace core

namespace std
{
    /** @brief Hashes a @ref core::string_id by its precomputed value, so it keys unordered containers for free. */
    template<>
    struct hash<core::string_id>
    {
        std::size_t operator()(const core::string_id& id) const noexcept
        {
            return static_cast<std::size_t>(id.value());
        }
    };
} // namespace std
