// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
