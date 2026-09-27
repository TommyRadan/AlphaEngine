// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file buffer.hpp
 * @brief @c gpu::buffer resource and its create-time descriptor.
 */

#pragma once

#include <cstddef>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
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
} // namespace rendering_engine::gpu
