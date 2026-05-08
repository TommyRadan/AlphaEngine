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
 * @file factory.cpp
 * @brief Backend dispatch for @ref rendering_engine::gpu::create_device.
 *
 * Each concrete backend lives behind a small @c make_*_device free
 * function in its own translation unit (@c gl_factory.cpp,
 * @c vk_factory.cpp). Both backends are always linked into the
 * binary; the runtime choice is driven by
 * @c settings::get_graphics_backend(), which the engine consults at
 * construction time.
 */

#include <stdexcept>

#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    std::unique_ptr<device> make_gl_device();
} // namespace rendering_engine::gpu::backend::opengl

namespace rendering_engine::gpu::backend::vulkan
{
    std::unique_ptr<device> make_vk_device();
} // namespace rendering_engine::gpu::backend::vulkan

namespace rendering_engine::gpu
{
    std::unique_ptr<device> create_device(backend_type type)
    {
        switch (type)
        {
        case backend_type::opengl:
            return backend::opengl::make_gl_device();
        case backend_type::vulkan:
            return backend::vulkan::make_vk_device();
        }
        LOG_FTL("create_device: unknown backend_type");
        throw std::runtime_error{"create_device: unknown backend_type"};
    }
} // namespace rendering_engine::gpu
