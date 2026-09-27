// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file bind_group.hpp
 * @brief Typed binding tables — the per-draw resource binding model.
 *
 * Layouts identify slots purely by binding number; the engine ships
 * SPIR-V with explicit @c layout(set, binding) decorations, so no
 * reflection happens at create-pipeline time.
 *
 * The supported value kinds are intentionally narrow: a single
 * @c uniform_buffer kind for plain-data uniforms (a UBO managed by the
 * caller), plus @c texture and @c sampler.
 *
 * A buffer binding may cover a sub-range of its buffer (@ref
 * binding_value::offset / @ref binding_value::size), and a uniform-buffer
 * slot whose layout entry sets @ref bind_group_layout_entry::has_dynamic_offset
 * takes an extra offset at bind time (@c render_pass_encoder::set_bind_group's
 * @c dynamic_offsets), so one bind group over one large buffer can serve
 * many draws that each read their own slice. Vulkan binds such a slot as
 * a @c VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC descriptor.
 */

#pragma once

#include <cstddef>
#include <cstdint>
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
    // the SPIR-V @c Binding decoration.
    struct bind_group_layout_entry
    {
        uint32_t binding{0};
        binding_kind kind{binding_kind::uniform_buffer};

        // For @c storage_texture only: the image format, matched
        // against the SPIR-V image @c Format decoration. Ignored for
        // other kinds.
        texture_format storage_format{texture_format::rgba8_unorm};

        // For @c storage_texture only: shader-side access mode.
        // Ignored for other kinds.
        storage_access storage_access_mode{storage_access::read_write};

        // For @c texture only: the sampler dimensionality the shader
        // declares (e.g. @c samplerCube vs @c sampler2D). The backend
        // uses it to pick a matching placeholder when a bind group
        // leaves this slot unset. Defaults to 2D.
        texture_dimension dimension{texture_dimension::d2};

        // The shader stages that read this binding. The Vulkan backend
        // bakes it into the descriptor-set layout's @c stageFlags, so a
        // layout must name every stage that declares the binding: the
        // default covers the vertex + fragment pair of a rasterisation
        // pipeline, a compute layout sets @c shader_stages_compute, and
        // a geometry / tessellation consumer widens it.
        shader_stages stages{shader_stages_default};

        // For @c uniform_buffer only: the slot takes a dynamic offset at
        // bind time, added to the bound range's @c offset (see
        // @c render_pass_encoder::set_bind_group). A bind group over such
        // a layout is written once and rebound per draw with a different
        // offset instead of being rebuilt. The value bound here must give
        // an explicit @c binding_value::size — the range each draw reads —
        // and every dynamic offset must be a multiple of
        // @c device_limits::uniform_buffer_offset_alignment. Two layouts
        // are only interchangeable when they agree on this flag. Ignored
        // for other kinds.
        bool has_dynamic_offset{false};
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
        // defaults to the base level.
        uint32_t storage_level{0};

        // For @c uniform_buffer / @c storage_buffer only: the byte range
        // of @ref buffer_value the slot exposes. @c offset must be a
        // multiple of the matching @c device_limits offset alignment; a
        // @c size of 0 covers the rest of the buffer from @c offset. The
        // defaults bind the whole buffer. A dynamic slot (see
        // @ref bind_group_layout_entry::has_dynamic_offset) needs a
        // non-zero @c size: each bind adds its dynamic offset to
        // @c offset and exposes @c size bytes from there.
        size_t offset{0};
        size_t size{0};
    };

    struct bind_group_descriptor
    {
        bind_group_layout layout{};
        std::vector<binding_value> entries;
    };
} // namespace rendering_engine::gpu
