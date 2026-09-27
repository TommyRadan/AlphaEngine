// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file shader.hpp
 * @brief Shader-module descriptor and the vertex-input layout used by
 *        @ref pipeline_descriptor.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    struct shader_module_descriptor
    {
        shader_stage stage{shader_stage::vertex};

        // SPIR-V byte blob, produced upstream by
        // @ref rendering_engine::gpu::compile_glsl_to_spirv.
        std::vector<uint32_t> spirv;
    };

#if defined(_DEBUG)
    // New code for a live shader module: what the debug hot reload hands
    // @c device::reload_shader_modules once an edited source recompiled.
    // The stage stays the module's own.
    struct shader_module_update
    {
        shader_module module{};
        std::vector<uint32_t> spirv;
    };
#endif

    // One attribute fed into the vertex shader. @ref location is the
    // shader-side input slot (matches GLSL @c layout(location=N)).
    // @ref components is the per-vertex element count (e.g. 3 for a
    // @c vec3 position) and @ref type the scalar type of each
    // component. @ref offset is the byte offset within the vertex
    // record.
    struct vertex_attribute
    {
        uint32_t location{0};
        uint32_t components{0};
        scalar_type type{scalar_type::float32};
        uint32_t offset{0};

        // For the integer @ref type values only. When true the fetch
        // converts each integer to a float in [0, 1] (unsigned) or
        // [-1, 1] (signed) — packed colours, quantised normals — and
        // the shader declares a float input. When false an integer
        // attribute is fetched as an integer and the shader declares an
        // @c ivec / @c uvec input. Ignored for @c float32. Selects the
        // @c _UNORM / @c _SNORM versus @c _UINT / @c _SINT Vulkan vertex
        // formats.
        bool normalized{false};
    };

    // How a vertex buffer slot advances through its attributes. @c vertex
    // steps once per vertex (the default); @c instance steps once per
    // instance, so a single record feeds every vertex of one instanced
    // draw copy. Maps to @c VkVertexInputRate.
    enum class vertex_step_mode
    {
        vertex,
        instance,
    };

    // Layout of one vertex buffer slot fed into a pipeline. The
    // pipeline references its layouts by index; the runtime
    // @c set_vertex_buffer call binds a buffer to the same slot.
    struct vertex_buffer_layout
    {
        uint32_t stride{0};
        vertex_step_mode step_mode{vertex_step_mode::vertex};
        std::vector<vertex_attribute> attributes;
    };
} // namespace rendering_engine::gpu
