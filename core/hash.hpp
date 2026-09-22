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
 * @file hash.hpp
 * @brief 64-bit FNV-1a, for content-addressed keys.
 *
 * A small, dependency-free non-cryptographic hash. It is what the shader
 * compiler keys its on-disk SPIR-V cache with; anything else that needs
 * a stable digest of some bytes (cache keys, dedup identities) can use
 * it too. Not for adversarial input.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace core
{
    constexpr uint64_t fnv1a_64_offset_basis = 14695981039346656037ULL;
    constexpr uint64_t fnv1a_64_prime = 1099511628211ULL;

    /**
     * @brief Hash @p bytes with FNV-1a, continuing from @p seed.
     *
     * Chaining calls by passing the previous result as @p seed hashes
     * the concatenation of the inputs.
     */
    constexpr uint64_t fnv1a_64(std::string_view bytes, uint64_t seed = fnv1a_64_offset_basis) noexcept
    {
        uint64_t hash = seed;
        for (const char c : bytes)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= fnv1a_64_prime;
        }
        return hash;
    }

    /**
     * @brief Incremental FNV-1a over a sequence of byte ranges and
     *        trivially-copyable values.
     *
     * Every @ref mix call folds more bytes into the running state, so the
     * final @ref value is the hash of everything mixed in, in order.
     */
    struct fnv1a_64_hasher
    {
        uint64_t state{fnv1a_64_offset_basis};

        void mix(std::string_view bytes) noexcept
        {
            state = fnv1a_64(bytes, state);
        }

        void mix(const void* data, std::size_t size) noexcept
        {
            mix(std::string_view{static_cast<const char*>(data), size});
        }

        /**
         * @brief Fold the object representation of @p v into the hash.
         *
         * Only for trivially-copyable types with no padding of concern
         * (integers, enums, floats): the bytes are hashed as stored.
         */
        template<typename T>
        void mix_value(const T& v) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "mix_value hashes the object representation");
            mix(&v, sizeof(T));
        }

        uint64_t value() const noexcept
        {
            return state;
        }
    };
} // namespace core
