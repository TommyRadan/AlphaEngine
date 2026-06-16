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
 * @file bind_group.hpp
 * @brief Typed binding tables — the per-draw resource binding model.
 *
 * Layouts identify slots purely by binding number; the engine ships
 * SPIR-V with explicit Vulkan-style @c layout(set, binding) decorations,
 * so the OpenGL backend's @c glBindBufferBase / @c glActiveTexture calls
 * use the binding number verbatim and no @c glGetUniformLocation
 * reflection happens at create-pipeline time.
 *
 * The supported value kinds are intentionally narrow: a single
 * @c uniform_buffer kind for plain-data uniforms (a UBO managed by the
 * caller), plus @c texture and @c sampler.
 */

#pragma once

#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu
{
    // What a single binding entry stores.
    enum class binding_kind
    {
        uniform_buffer,
        texture,
        sampler,
        storage_buffer,
        storage_texture,
    };

    // One slot in a layout. The binding number maps directly onto
    // the SPIR-V @c Binding decoration and onto the OpenGL binding
    // point (UBO / SSBO unit for @c uniform_buffer / @c storage_buffer,
    // texture image unit for @c texture / @c sampler, image unit for
    // @c storage_texture).
    struct bind_group_layout_entry
    {
        uint32_t binding{0};
        binding_kind kind{binding_kind::uniform_buffer};

        // For @c storage_texture only: the image format used by
        // @c glBindImageTexture and matched against the SPIR-V
        // image @c Format decoration. Ignored for other kinds.
        texture_format storage_format{texture_format::rgba8_unorm};

        // For @c storage_texture only: shader-side access mode.
        // Ignored for other kinds.
        storage_access storage_access_mode{storage_access::read_write};

        // For @c texture only: the sampler dimensionality the shader
        // declares (e.g. @c samplerCube vs @c sampler2D). Explicit-
        // binding backends use it to pick a matching placeholder when a
        // bind group leaves this slot unset; the OpenGL backend ignores
        // it. Defaults to 2D.
        texture_dimension dimension{texture_dimension::d2};
    };

    struct bind_group_layout_descriptor
    {
        std::vector<bind_group_layout_entry> entries;
    };

    // A concrete value for one binding slot. Only the field matching
    // @ref kind is read. The non-active fields are present to keep
    // the type a plain aggregate so call sites can use designated
    // initializers without juggling a tagged union.
    struct binding_value
    {
        uint32_t binding{0};
        binding_kind kind{binding_kind::uniform_buffer};

        gpu::buffer buffer_value{};
        gpu::texture texture_value{};
        gpu::sampler sampler_value{};

        // For @c storage_texture only: the mip level to bind as the
        // image. Cube and 3D storage images bind every layer (the IBL
        // compute writes a whole cube level through an @c imageCube), so
        // only the level needs selecting. Ignored for other kinds and
        // defaults to the base level to preserve existing bindings.
        uint32_t storage_level{0};
    };

    struct bind_group_descriptor
    {
        bind_group_layout layout{};
        std::vector<binding_value> entries;
    };
} // namespace rendering_engine::gpu
