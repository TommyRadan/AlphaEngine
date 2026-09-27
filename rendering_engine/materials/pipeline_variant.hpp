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
 * @file pipeline_variant.hpp
 * @brief The pure, device-free half of the material system: the keyword
 *        bits a material can compile its shaders with, the key that names
 *        one pipeline variant of a @ref material_template, and the
 *        translation of the shared @ref material_params surface onto
 *        fixed-function GPU state.
 *
 * Everything here is plain data and constexpr-friendly so the unit tests
 * can exercise the key encoding, the keyword-to-define mapping and the
 * blend / depth / rasterizer mapping without a device.
 */

#pragma once

#include <array>
#include <cstdint>

#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    // Named blend equation presets. @c normal is straight alpha
    // compositing; the rest map onto the matching source/destination
    // factors (see @ref to_blend_state). @c none leaves blending
    // disabled regardless of @c transparent.
    enum class blend_mode : uint8_t
    {
        none,
        normal,
        additive,
        subtractive,
        multiply,
    };

    // The shared parameter surface every material instance carries.
    // These are data-only knobs; @ref make_pipeline_variant_key folds
    // them into the variant key and @ref to_depth_state /
    // @ref to_blend_state / @ref to_rasterizer_state translate the key
    // onto the fixed-function state baked into the pipeline. Every field
    // is mutable at runtime through the @ref material setters: a change
    // that alters the key rebinds the instance to another variant of its
    // template (looked up, or built once, in the template's cache).
    struct material_params
    {
        // Whether the surface participates in alpha blending. When
        // false the pipeline blend stage stays disabled (the object
        // is opaque) no matter what @c blending selects.
        bool transparent{false};

        // Surface opacity in [0, 1]. Not part of the variant key: it is
        // uploaded into the material's parameter block for shaders that
        // consume it (the standard material multiplies it into the
        // output alpha).
        float opacity{1.0f};

        // Disable back-face culling so both faces rasterize. Maps to
        // @c cull_mode::none; otherwise back faces are culled.
        bool double_sided{false};

        // Blend equation preset applied when @c transparent is set.
        blend_mode blending{blend_mode::normal};

        // Rasterize edges only. Maps to @c polygon_mode::line and
        // defines @c WIREFRAME for the shaders.
        bool wireframe{false};

        // Depth comparison against the existing buffer. When off the
        // surface always passes the depth test.
        bool depth_test{true};

        // Whether passing fragments write their depth back. Ignored when
        // @c transparent is set: @ref make_pipeline_variant_key forces the
        // pipeline's depth write off for the transparent queue regardless
        // of this value, so a caller that sets one without the other still
        // gets correct back-to-front blending.
        bool depth_write{true};

        // Whether the scene fog blends over the surface (three.js
        // Material.fog). Off, the lit materials compile a variant with
        // no fog code at all (@c NO_FOG) — for skyboxes, in-world UI,
        // emissive markers. The unlit materials never apply fog, so the
        // flag has no effect on them.
        bool fog{true};
    };

    // Shader keywords: each bit becomes a @c #define injected ahead of
    // both stages when a variant is compiled (@ref keyword_defines), so
    // a material that has no normal map compiles a shader with no normal-
    // map sampling rather than branching on a uniform per fragment. The
    // bit positions are stable — they are part of the variant key and of
    // the SPIR-V cache digest through the define text.
    enum class material_keyword : uint32_t
    {
        use_albedo_map = 1u << 0,
        use_normal_map = 1u << 1,
        use_metallic_map = 1u << 2,
        use_roughness_map = 1u << 3,
        use_emissive_map = 1u << 4,
        use_occlusion_map = 1u << 5,

        // One packed occlusion / roughness / metallic map (R / G / B,
        // the glTF convention) bound at the metalness slot. Replaces
        // the metallic and roughness maps above and supplies occlusion
        // from R unless @c use_occlusion_map is set as well.
        use_orm_map = 1u << 6,

        // The vertex stream carries a tangent (location 3) so the
        // normal map can build its TBN basis. Without it the pipeline
        // reads a tangent-less record and normal mapping is off.
        has_tangents = 1u << 7,

        // Wireframe rasterization (@c polygon_mode::line); shaders may
        // shade the edges unlit. Set from @ref material_params::wireframe.
        wireframe = 1u << 8,

        // Skeletal skinning: the vertex stream carries four joint indices
        // and weights (the skinned record) and the per-draw group a
        // joint-matrix palette the vertex stage blends by. A template
        // that supports it swaps its vertex and per-draw layouts for the
        // skinned ones (see @ref material_template_descriptor).
        skinned = 1u << 9,

        // The surface ignores the scene fog: the lit fragment shaders
        // skip the fog blend, so the variant carries no fog code.
        // Inverted (rather than USE_FOG) so the default, fogged variant
        // keeps a zero keyword mask and the SPIR-V it always had. Set
        // from @ref material_params::fog.
        no_fog = 1u << 10,
    };

    constexpr uint32_t keyword_bit(material_keyword keyword)
    {
        return static_cast<uint32_t>(keyword);
    }

    constexpr uint32_t material_keyword_count = 11;

    // Every keyword, in bit order.
    constexpr std::array<material_keyword, material_keyword_count> all_material_keywords = {
        material_keyword::use_albedo_map,
        material_keyword::use_normal_map,
        material_keyword::use_metallic_map,
        material_keyword::use_roughness_map,
        material_keyword::use_emissive_map,
        material_keyword::use_occlusion_map,
        material_keyword::use_orm_map,
        material_keyword::has_tangents,
        material_keyword::wireframe,
        material_keyword::skinned,
        material_keyword::no_fog,
    };

    // The preprocessor symbol @p keyword defines (@c "USE_ALBEDO_MAP", ...).
    constexpr const char* keyword_define(material_keyword keyword)
    {
        switch (keyword)
        {
        case material_keyword::use_albedo_map:
            return "USE_ALBEDO_MAP";
        case material_keyword::use_normal_map:
            return "USE_NORMAL_MAP";
        case material_keyword::use_metallic_map:
            return "USE_METALLIC_MAP";
        case material_keyword::use_roughness_map:
            return "USE_ROUGHNESS_MAP";
        case material_keyword::use_emissive_map:
            return "USE_EMISSIVE_MAP";
        case material_keyword::use_occlusion_map:
            return "USE_OCCLUSION_MAP";
        case material_keyword::use_orm_map:
            return "USE_ORM_MAP";
        case material_keyword::has_tangents:
            return "HAS_TANGENTS";
        case material_keyword::wireframe:
            return "WIREFRAME";
        case material_keyword::skinned:
            return "SKINNED";
        case material_keyword::no_fog:
            return "NO_FOG";
        }
        return "";
    }

    // The defines for every keyword set in @p mask, in bit order,
    // appended to @p base (a template's fixed defines). Each is a flag
    // (empty value), so a shader tests it with @c #ifdef.
    gpu::shader_defines keyword_defines(uint32_t mask, const gpu::shader_defines& base = {});

    // Names one pipeline of a @ref material_template: the keyword set
    // the shaders were compiled with plus every piece of fixed-function
    // state the pipeline bakes. Two instances whose params and keywords
    // produce the same key share one pipeline object.
    struct pipeline_variant_key
    {
        uint32_t keywords{0};
        blend_mode blending{blend_mode::none};
        gpu::cull_mode cull{gpu::cull_mode::back};
        gpu::front_face front{gpu::front_face::counter_clockwise};
        gpu::polygon_mode polygon{gpu::polygon_mode::fill};
        bool depth_test{true};
        bool depth_write{true};

        // The depth comparison while @c depth_test is on. Never chosen by
        // @ref material_params: every key @ref make_pipeline_variant_key
        // builds compares @c less, and only the depth pre-pass twins
        // (@ref depth_prepassed_variant) ask for another one.
        gpu::compare_function depth_compare{gpu::compare_function::less};

        // The pipeline has no fragment stage: the variant's vertex shader
        // alone rasterizes depth into a depth-only target. Set only on the
        // depth pre-pass twin (@ref depth_prepass_variant), which draws the
        // same vertex module as the colour variant so both pass the same
        // depth for every fragment.
        bool depth_only{false};

        // The key as one 64-bit word: the keyword mask in the low 32
        // bits, the fixed-function fields packed above it. Unique per
        // distinct key, so it serves as the cache's hash key directly.
        uint64_t pack() const;

        friend bool operator==(const pipeline_variant_key& a, const pipeline_variant_key& b) = default;
    };

    // Builds the key for @p params and the instance's @p keywords, drawing
    // with @p front as the front-facing winding. Folds @c transparent
    // into @c blending (opaque reads as @c none) and forces @c depth_write
    // off regardless of the param, @c double_sided into the cull mode,
    // @c wireframe into both the polygon mode and the @c WIREFRAME
    // keyword, and a cleared @c fog into the @c NO_FOG keyword. @c opacity
    // does not take part: it lives in the parameter block, not the
    // pipeline. The key compares depth with @c less and has a fragment
    // stage.
    pipeline_variant_key make_pipeline_variant_key(const material_params& params,
                                                   uint32_t keywords,
                                                   gpu::front_face front = gpu::front_face::counter_clockwise);

    // The depth comparison the scene pass draws depth pre-passed geometry
    // with. @c less_equal rather than @c equal: the pre-pass and the scene
    // pass run the same vertex module with an invariant @c gl_Position, so
    // a surface's own fragments compare equal and pass either way, and
    // anything behind the nearest surface fails either way; @c less_equal
    // only differs where a fragment would come out nearer than the stored
    // depth, where it still shades the surface instead of leaving a hole.
    constexpr gpu::compare_function depth_prepassed_compare = gpu::compare_function::less_equal;

    // The depth pre-pass twin of @p key: the same keywords (so the same
    // vertex module and vertex layout), cull mode, front face and polygon
    // mode, but @c depth_only, depth-tested with @c less and written, and
    // with no blending (there is no colour attachment to blend into).
    pipeline_variant_key depth_prepass_variant(const pipeline_variant_key& key);

    // The scene-pass twin of @p key for geometry the depth pre-pass has
    // already laid down: the same state with depth writes off and the
    // comparison relaxed to @ref depth_prepassed_compare, so each fragment
    // passes only where its surface is the nearest one the pre-pass
    // stored, and shades exactly once.
    pipeline_variant_key depth_prepassed_variant(const pipeline_variant_key& key);

    // Fixed-function state for a key. Depth is encoded once: the test is
    // enabled or not, and the comparison is the key's @c depth_compare
    // (@c less for every key a material's params produce). Blend maps
    // each preset onto its factors — @c normal is src_alpha /
    // one_minus_src_alpha, @c additive src_alpha / one, @c subtractive
    // src_alpha / one with @c reverse_subtract (destination minus the
    // alpha-weighted source, the mirror of additive), @c multiply zero /
    // src_color. The rasterizer takes the key's cull, front face and
    // polygon mode verbatim.
    gpu::depth_state to_depth_state(const pipeline_variant_key& key);
    gpu::blend_state to_blend_state(blend_mode mode);
    gpu::rasterizer_state to_rasterizer_state(const pipeline_variant_key& key);
} // namespace rendering_engine
