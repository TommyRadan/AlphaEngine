// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file handle.hpp
 * @brief Opaque, strongly-typed GPU resource handles.
 *
 * Resources allocated by @ref rendering_engine::gpu::device are returned as
 * @ref handle values rather than raw pointers or backend-native object IDs.
 * The 64-bit payload encodes a pool index plus a generation counter so that
 * use-after-destroy is detected by the backend rather than corrupting an
 * unrelated, recycled resource. The tag template parameter prevents passing
 * a buffer handle where a texture handle is expected.
 */

#pragma once

#include <cstdint>

namespace rendering_engine::gpu
{
    template<typename Tag>
    struct handle
    {
        uint64_t id{0};

        constexpr bool valid() const noexcept
        {
            return id != 0;
        }

        constexpr bool operator==(const handle& other) const noexcept
        {
            return id == other.id;
        }

        constexpr bool operator!=(const handle& other) const noexcept
        {
            return id != other.id;
        }
    };

    struct buffer_tag
    {
    };
    struct texture_tag
    {
    };
    struct sampler_tag
    {
    };
    struct shader_module_tag
    {
    };
    struct pipeline_tag
    {
    };
    struct bind_group_layout_tag
    {
    };
    struct bind_group_tag
    {
    };
    struct render_target_tag
    {
    };
    struct query_set_tag
    {
    };

    using buffer = handle<buffer_tag>;
    using texture = handle<texture_tag>;
    using sampler = handle<sampler_tag>;
    using shader_module = handle<shader_module_tag>;
    using pipeline = handle<pipeline_tag>;
    using bind_group_layout = handle<bind_group_layout_tag>;
    using bind_group = handle<bind_group_tag>;
    using render_target = handle<render_target_tag>;
    using query_set = handle<query_set_tag>;
} // namespace rendering_engine::gpu
