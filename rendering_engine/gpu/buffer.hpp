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
 * @file buffer.hpp
 * @brief @c gpu::buffer resource and its create-time descriptor.
 */

#pragma once

#include <cstddef>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct buffer_descriptor
        {
            // Length of the allocation in bytes.
            size_t size{0};

            // Bitmask of @c buffer_usage_* flags describing which bind
            // points the buffer must be valid for. A vertex buffer that
            // also accepts streaming updates would set
            // @c buffer_usage_vertex | @c buffer_usage_copy_dst.
            buffer_usage usage{0};

            // Backend hint for memory placement / driver heuristics.
            buffer_usage_hint hint{buffer_usage_hint::static_data};

            // Optional initial contents. If non-null and @ref size > 0
            // the backend uploads it during creation; otherwise the
            // buffer starts uninitialized and must be written before use.
            const void* initial_data{nullptr};
        };
    } // namespace gpu
} // namespace rendering_engine
