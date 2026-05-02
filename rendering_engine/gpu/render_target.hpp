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
 * @file render_target.hpp
 * @brief Render-target descriptors and per-attachment load/store ops.
 *
 * The engine currently renders to the default swapchain target only;
 * @c device::swapchain_target() returns the handle for it. Off-screen
 * targets (offscreen colour + optional depth attachments) will plug in
 * here without changing the @c render_pass_descriptor surface.
 */

#pragma once

#include <array>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct render_target_descriptor
        {
            // Color attachment format and dimensions. For the swapchain
            // these come from the window; for offscreen targets the
            // caller fills them in.
            texture_format color_format{texture_format::rgba8_unorm};
            uint32_t width{0};
            uint32_t height{0};

            // Whether the target needs a depth attachment. The format
            // defaults to a 24-bit depth buffer to match the existing
            // swapchain configuration.
            bool with_depth{true};
            texture_format depth_format{texture_format::depth24};
        };

        struct color_attachment_op
        {
            load_op load{load_op::clear};
            store_op store{store_op::store};
            std::array<float, 4> clear_color{0.0f, 0.0f, 0.0f, 1.0f};
        };

        struct depth_attachment_op
        {
            load_op load{load_op::clear};
            store_op store{store_op::store};
            float clear_depth{1.0f};
        };

        struct render_pass_descriptor
        {
            render_target target{};
            color_attachment_op color;
            depth_attachment_op depth;

            // When false the pass skips depth state changes regardless
            // of the target's depth attachment (e.g. UI overlay pass).
            bool use_depth{true};
        };
    } // namespace gpu
} // namespace rendering_engine
