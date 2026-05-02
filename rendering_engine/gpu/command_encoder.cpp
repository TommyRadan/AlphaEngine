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
 * @file command_encoder.cpp
 * @brief Anchor for the @ref rendering_engine::gpu::command_encoder
 *        and @ref rendering_engine::gpu::render_pass_encoder abstract
 *        interfaces — defines the out-of-line virtual destructors so
 *        the vtables and typeinfos are emitted in this single
 *        translation unit instead of every TU that includes
 *        @c command_encoder.hpp.
 */

#include <rendering_engine/gpu/command_encoder.hpp>

namespace rendering_engine::gpu
{
    render_pass_encoder::~render_pass_encoder() = default;

    command_encoder::~command_encoder() = default;
} // namespace rendering_engine::gpu
