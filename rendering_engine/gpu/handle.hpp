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

    using buffer = handle<buffer_tag>;
    using texture = handle<texture_tag>;
    using sampler = handle<sampler_tag>;
    using shader_module = handle<shader_module_tag>;
    using pipeline = handle<pipeline_tag>;
    using bind_group_layout = handle<bind_group_layout_tag>;
    using bind_group = handle<bind_group_tag>;
    using render_target = handle<render_target_tag>;
} // namespace rendering_engine::gpu
