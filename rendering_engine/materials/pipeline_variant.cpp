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

#include <rendering_engine/materials/pipeline_variant.hpp>

namespace rendering_engine
{
    gpu::shader_defines keyword_defines(uint32_t mask, const gpu::shader_defines& base)
    {
        gpu::shader_defines defines = base;
        for (const material_keyword keyword : all_material_keywords)
        {
            if ((mask & keyword_bit(keyword)) != 0)
            {
                defines.emplace_back(keyword_define(keyword), "");
            }
        }
        return defines;
    }

    uint64_t pipeline_variant_key::pack() const
    {
        // Bit layout: [0, 32) keywords, [32, 35) blending, [35, 37) cull,
        // [37] front face, [38, 40) polygon, [40] depth test, [41] depth
        // write. Every field fits its width with room to spare.
        uint64_t packed = keywords;
        packed |= static_cast<uint64_t>(blending) << 32;
        packed |= static_cast<uint64_t>(cull) << 35;
        packed |= static_cast<uint64_t>(front) << 37;
        packed |= static_cast<uint64_t>(polygon) << 38;
        packed |= static_cast<uint64_t>(depth_test ? 1u : 0u) << 40;
        packed |= static_cast<uint64_t>(depth_write ? 1u : 0u) << 41;
        return packed;
    }

    pipeline_variant_key
    make_pipeline_variant_key(const material_params& params, uint32_t keywords, gpu::front_face front)
    {
        pipeline_variant_key key{};
        key.keywords = keywords;
        if (params.wireframe)
        {
            key.keywords |= keyword_bit(material_keyword::wireframe);
        }
        else
        {
            key.keywords &= ~keyword_bit(material_keyword::wireframe);
        }
        // Like WIREFRAME, the fog keyword follows the param, never the
        // caller's mask, so a fogged surface always compiles the fog blend
        // and a fog-less one never does.
        if (params.fog)
        {
            key.keywords &= ~keyword_bit(material_keyword::no_fog);
        }
        else
        {
            key.keywords |= keyword_bit(material_keyword::no_fog);
        }
        key.blending = params.transparent ? params.blending : blend_mode::none;
        key.cull = params.double_sided ? gpu::cull_mode::none : gpu::cull_mode::back;
        key.front = front;
        key.polygon = params.wireframe ? gpu::polygon_mode::line : gpu::polygon_mode::fill;
        key.depth_test = params.depth_test;
        // Transparent surfaces never write depth, whatever the param says:
        // forcing it here — rather than trusting a caller to also flip
        // @c depth_write whenever it flips @c transparent — means the
        // transparent queue's back-to-front draws always blend against
        // what is already in the depth buffer instead of occluding one
        // another as they draw, even for a material a caller only set
        // @c transparent on.
        key.depth_write = params.transparent ? false : params.depth_write;
        return key;
    }

    gpu::depth_state to_depth_state(const pipeline_variant_key& key)
    {
        gpu::depth_state depth{};
        depth.test_enabled = key.depth_test;
        depth.write_enabled = key.depth_write;
        depth.compare = gpu::compare_function::less;
        return depth;
    }

    gpu::blend_state to_blend_state(blend_mode mode)
    {
        gpu::blend_state blend{};
        blend.enabled = mode != blend_mode::none;
        if (!blend.enabled)
        {
            return blend;
        }

        blend.op = gpu::blend_op::add;
        switch (mode)
        {
        case blend_mode::additive:
            blend.src = gpu::blend_factor::src_alpha;
            blend.dst = gpu::blend_factor::one;
            break;
        case blend_mode::subtractive:
            // dst - src * alpha: darkens by the alpha-weighted source,
            // the mirror of additive.
            blend.op = gpu::blend_op::reverse_subtract;
            blend.src = gpu::blend_factor::src_alpha;
            blend.dst = gpu::blend_factor::one;
            break;
        case blend_mode::multiply:
            blend.src = gpu::blend_factor::zero;
            blend.dst = gpu::blend_factor::src_color;
            break;
        case blend_mode::normal:
        case blend_mode::none:
            blend.src = gpu::blend_factor::src_alpha;
            blend.dst = gpu::blend_factor::one_minus_src_alpha;
            break;
        }
        return blend;
    }

    gpu::rasterizer_state to_rasterizer_state(const pipeline_variant_key& key)
    {
        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = key.cull;
        rasterizer.front = key.front;
        rasterizer.polygon = key.polygon;
        return rasterizer;
    }
} // namespace rendering_engine
