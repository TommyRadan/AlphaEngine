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
 * @file device.cpp
 * @brief Anchor for the @ref rendering_engine::gpu::device abstract
 *        interface — defines the out-of-line virtual destructor so the
 *        vtable and typeinfo are emitted in this single translation
 *        unit instead of every TU that includes @c device.hpp, plus
 *        the default bodies of the optional virtuals (the debug-name
 *        hooks a backend without labels leaves alone, and the debug
 *        hot-reload hooks of a backend without hot reload).
 */

#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::gpu
{
    device::~device() = default;

    void device::set_debug_name(buffer /*handle*/, const char* /*name*/) {}

    void device::set_debug_name(texture /*handle*/, const char* /*name*/) {}

    void device::set_debug_name(sampler /*handle*/, const char* /*name*/) {}

    void device::set_debug_name(pipeline /*handle*/, const char* /*name*/) {}

    void device::set_debug_name(render_target /*handle*/, const char* /*name*/) {}

#if defined(_DEBUG)
    bool device::reload_shader_modules(const std::vector<shader_module_update>& /*updates*/)
    {
        return false;
    }

    bool device::shader_module_live(shader_module /*module*/)
    {
        return false;
    }
#endif
} // namespace rendering_engine::gpu
