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

#pragma once

#include <cstdint>
#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>

namespace rendering_engine
{
    // Named blend equation presets. @c normal is straight alpha
    // compositing; the rest map onto the matching source/destination
    // factors. @c none leaves blending disabled regardless of
    // @c transparent.
    enum class blend_mode
    {
        none,
        normal,
        additive,
        subtractive,
        multiply,
    };

    // The shared parameter surface every material inherits. These are
    // data-only knobs; the base translates them onto the @c gpu::depth_state /
    // @c gpu::blend_state / @c gpu::rasterizer_state baked into the
    // pipeline (see @ref material::construct_pipeline). @c opacity is
    // stored for materials whose shaders consume it; the fixed-function
    // mapping does not read it on its own.
    struct material_params
    {
        // Whether the surface participates in alpha blending. When
        // false the pipeline blend stage stays disabled (the object
        // is opaque) no matter what @c blending selects.
        bool transparent{false};

        // Surface opacity in [0, 1]. Pure data — consumed by shaders
        // that read it, not by the blend state.
        float opacity{1.0f};

        // Disable back-face culling so both faces rasterize. Maps to
        // @c cull_mode::none; otherwise back faces are culled.
        bool double_sided{false};

        // Blend equation preset applied when @c transparent is set.
        blend_mode blending{blend_mode::normal};

        // Rasterize edges only. Maps to @c polygon_mode::line.
        bool wireframe{false};

        // Depth comparison against the existing buffer. When off the
        // surface always passes the depth test.
        bool depth_test{true};

        // Whether passing fragments write their depth back.
        bool depth_write{true};
    };

    // A material owns a @ref gpu::pipeline (shader + fixed-function
    // state + bind-group layouts) and the per-draw bind-group layout
    // that renderables build their @ref draw_item against. The
    // optional per-material bind group is reserved for shared
    // resources (textures, parameter UBOs) that don't change between
    // draws — none of the built-in materials use it yet.
    struct material
    {
        virtual ~material();

        material(const material&) = delete;
        material& operator=(const material&) = delete;

        gpu::pipeline pipeline() const;

        // The shared base parameters this material was built with.
        // Reflects whatever @ref construct_pipeline baked; mutating
        // it does not re-bake the pipeline.
        const material_params& params() const;

        // Layout renderables build their per-draw bind group
        // against (model matrix, per-draw textures, options).
        gpu::bind_group_layout per_draw_layout() const;

        // Slot index used for the per-draw bind group. Computed
        // from whether the material's pipeline reserves slot 0
        // for a per-frame bind group (1 if so, 0 otherwise).
        uint32_t per_draw_slot() const;

        // Optional per-material bind group; invalid when the
        // material has no shared per-material resources. Bound
        // by the dispatch loop after @c set_pipeline whenever the
        // pipeline changes.
        gpu::bind_group per_material_bind_group() const;

        // Slot index for the per-material bind group. Materials
        // that occupy slot 0 (no per-frame layout) and have a
        // per-material group typically place it at slot 1; with
        // a per-frame layout it would be slot 2. The default
        // implementation returns @ref per_draw_slot to keep an
        // unused per-material binding from accidentally clobbering
        // another slot.
        virtual uint32_t per_material_slot() const;

    protected:
        material() = default;

        // Build the pipeline + per-draw layout from the shared
        // @ref material_params surface. The params are stored (see
        // @ref params) and translated onto the fixed-function depth /
        // blend / rasterizer state before delegating to the explicit
        // overload below. Prefer this entry point for new materials.
        //
        // When @p material_layout has entries the pipeline reserves an
        // additional descriptor set for the optional per-material bind
        // group (see @ref m_per_material_layout); the layout is created,
        // stored, and exposed to the subclass so it can build its
        // @ref m_per_material_bind_group against it. Leave it empty for
        // materials with no shared per-material resources.
        //
        // @p topology selects the primitive assembly mode baked into
        // the pipeline; it defaults to @c triangles so existing
        // materials need no changes. Point-list materials pass
        // @c primitive_topology::points.
        void construct_pipeline(const std::string& vertex_source,
                                const std::string& fragment_source,
                                const gpu::vertex_buffer_layout& vertex_layout,
                                const gpu::bind_group_layout_descriptor& draw_layout,
                                gpu::bind_group_layout frame_layout,
                                const material_params& params,
                                const gpu::bind_group_layout_descriptor& material_layout = {},
                                gpu::primitive_topology topology = gpu::primitive_topology::triangles);

        // Build the pipeline + per-draw layout from source. When
        // @p frame_layout is valid, slot 0 is reserved for a
        // per-frame bind group owned by the corresponding pass and
        // @ref per_draw_slot returns 1; otherwise the per-draw
        // group occupies slot 0. When @p material_layout has entries
        // it becomes the trailing descriptor set (per-material).
        // @p topology bakes the primitive assembly mode into the
        // pipeline (defaults to @c triangles).
        void construct_pipeline(const std::string& vertex_source,
                                const std::string& fragment_source,
                                const gpu::vertex_buffer_layout& vertex_layout,
                                const gpu::bind_group_layout_descriptor& draw_layout,
                                gpu::bind_group_layout frame_layout,
                                const gpu::depth_state& depth,
                                const gpu::blend_state& blend,
                                const gpu::rasterizer_state& rasterizer,
                                const gpu::bind_group_layout_descriptor& material_layout = {},
                                gpu::primitive_topology topology = gpu::primitive_topology::triangles);

        void destruct_pipeline();

        material_params m_params{};
        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::pipeline m_pipeline{};
        gpu::bind_group_layout m_per_draw_layout{};

        // Optional trailing descriptor-set layout for shared
        // per-material resources. Valid only when @ref construct_pipeline
        // was handed a non-empty material layout; subclasses build
        // @ref m_per_material_bind_group against it. Released in
        // @ref destruct_pipeline.
        gpu::bind_group_layout m_per_material_layout{};
        gpu::bind_group m_per_material_bind_group{};

        // Whether the pipeline reserves slot 0 for a per-frame
        // bind group. Stored so @ref per_draw_slot stays correct
        // after @ref destruct_pipeline drops the layout handles.
        bool m_has_frame_layout{false};
    };
} // namespace rendering_engine
