// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file material_parameters.hpp
 * @brief The declared per-material resources of a template: its parameter
 *        block's members and its sampled textures.
 *
 * A template that declares them (@ref material_template_descriptor::parameters,
 * @ref material_template_descriptor::textures) gets its per-material bind
 * group layout derived from them, and every instance of it owns a parameter
 * buffer and one texture binding per slot, written through the instance's
 * setters by name (@ref material::set_float4, @ref material::set_texture,
 * ...). The shader side declares the same members in its per-material set
 * (see shaders/include/per_material.glsl):
 *
 * @code
 * layout(set = PER_MATERIAL_SET, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
 * {
 *     vec4 base_color;  // offset 0
 *     float rim_power;  // offset 16
 * } u_material;
 * layout(set = PER_MATERIAL_SET, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D albedo_map;
 * @endcode
 *
 * The layout is hand-declared: the offsets are the std140 offsets the
 * shader's block has, and each member must sit at a multiple of its std140
 * base alignment (@ref material_parameter_alignment).
 */

#pragma once

#include <cstdint>
#include <string>

#include <core/math/vec4.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    /** @brief The GLSL type of one parameter-block member. */
    enum class material_parameter_type : uint8_t
    {
        float1, /**< @c float */
        float2, /**< @c vec2 */
        float3, /**< @c vec3 */
        float4, /**< @c vec4 */
        int1,   /**< @c int */
        uint1,  /**< @c uint */
    };

    /** @brief The byte size of a @p type member. */
    constexpr uint32_t material_parameter_size(material_parameter_type type)
    {
        switch (type)
        {
        case material_parameter_type::float1:
        case material_parameter_type::int1:
        case material_parameter_type::uint1:
            return 4;
        case material_parameter_type::float2:
            return 8;
        case material_parameter_type::float3:
            return 12;
        case material_parameter_type::float4:
            return 16;
        }
        return 16;
    }

    /** @brief The std140 base alignment of a @p type member: a @c vec3 aligns like a @c vec4. */
    constexpr uint32_t material_parameter_alignment(material_parameter_type type)
    {
        switch (type)
        {
        case material_parameter_type::float1:
        case material_parameter_type::int1:
        case material_parameter_type::uint1:
            return 4;
        case material_parameter_type::float2:
            return 8;
        case material_parameter_type::float3:
        case material_parameter_type::float4:
            return 16;
        }
        return 16;
    }

    /** @brief One member of a template's parameter block. */
    struct material_parameter
    {
        /** @brief The name an instance's setters address it by. */
        std::string name;

        material_parameter_type type{material_parameter_type::float4};

        /** @brief Its std140 byte offset in the block. */
        uint32_t offset{0};

        /**
         * @brief The value a new instance starts with: the leading
         *        components for a float type, @c x converted for
         *        @c int1 / @c uint1.
         */
        core::math::vec4 default_value{0.0f, 0.0f, 0.0f, 0.0f};
    };

    /** @brief One sampled texture of a template's per-material set. */
    struct material_texture_slot
    {
        /** @brief The name @ref material::set_texture addresses it by. */
        std::string name;

        /**
         * @brief Its binding in the per-material set; one of the
         *        @c gpu::shader_bindings::material_*_map numbers, so it
         *        stays unique across the pipeline's sets.
         */
        uint32_t binding{0};

        /** @brief The sampler type the shader declares (@c sampler2D, @c samplerCube). */
        gpu::texture_dimension dimension{gpu::texture_dimension::d2};

        /**
         * @brief A keyword compiled into the variant while a texture is
         *        bound to the slot — an engine keyword's define
         *        (@c USE_ALBEDO_MAP) or one of the template's own
         *        (@ref material_template_descriptor::keywords) — or empty.
         *        With it the shader samples the slot under an @c #ifdef
         *        rather than testing a flag per fragment; an empty slot
         *        binds the device's placeholder texture.
         */
        std::string keyword;
    };
} // namespace rendering_engine
