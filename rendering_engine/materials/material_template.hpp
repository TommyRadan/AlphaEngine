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
 * @file material_template.hpp
 * @brief One material *type*: its shaders, vertex and bind-group layouts,
 *        and the cache of pipeline variants its instances draw with.
 *
 * A @ref material_template is built once per material type (the
 * renderer makes one standard, one phong, ... template in
 * @c context::init) and shared by every @ref material instance of that
 * type. It owns no per-instance state: an instance carries its own
 * parameter block, textures and per-material bind group and points at
 * the template for everything else.
 *
 * Pipelines are built lazily, keyed by @ref pipeline_variant_key. The
 * first instance to need a given (keyword set, fixed-function state)
 * compiles the shaders for that keyword set (shader modules are cached
 * per keyword mask, so two variants that differ only in blend or depth
 * state share one SPIR-V pair) and creates the pipeline; every later
 * instance with the same key gets the same handle. N instances of one
 * template with the same key therefore cost one pipeline, and a scene
 * of many standard materials sorts down to a handful of pipeline
 * switches.
 *
 * The modules are created through @ref gpu::create_library_shader_module,
 * so in a debug build an edit of a template's shader (or of an include it
 * reads) is swapped in behind the cached module handles and every variant
 * built from them is rebuilt in place (see shader_hot_reload.hpp); the
 * cache and the handles instances hold stay valid.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/materials/pipeline_variant.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct material;

    // Everything a material type declares up front. The template copies
    // it and derives the rest (layouts, strides) at construction.
    struct material_template_descriptor
    {
        // Sentinel for @ref tangent_location: the slot-0 layout carries
        // no optional tangent channel.
        static constexpr uint32_t no_tangent_location = UINT32_MAX;

        // Label for log lines.
        std::string name;

        // The two stages by shader-library path (see shaders/materials/)
        // plus the defines every variant of this template is compiled
        // with; the keyword defines are appended per variant.
        gpu::shader_variant vertex_shader;
        gpu::shader_variant fragment_shader;

        // Vertex buffer slots in declaration order. Slot 0 is the
        // per-vertex geometry stream every renderable binds; a per-
        // instance stream, if any, follows.
        std::vector<gpu::vertex_buffer_layout> vertex_layouts;

        // The record layout the slot-0 attributes read (see
        // @ref vertex_format), and the layout a variant without the
        // @c has_tangents keyword reads instead. Both @c custom when the
        // material declares none.
        vertex_format required_vertex_format{vertex_format::custom};
        vertex_format vertex_format_without_tangents{vertex_format::custom};

        // Attribute location of the optional tangent channel in slot 0.
        // A variant built without @ref material_keyword::has_tangents
        // drops this attribute from its layout (and reports
        // @ref vertex_format_without_tangents) so a tangent-less record
        // binds without any fetch running past the vertex.
        uint32_t tangent_location{no_tangent_location};

        // Per-draw bind-group layout (the slot after the per-frame group,
        // if any); may be empty for materials that carry their per-draw
        // data in a vertex stream.
        gpu::bind_group_layout_descriptor draw_layout;

        // Skinning support. A template that can skin lists the slot-0
        // attributes a variant with @ref material_keyword::skinned appends
        // (the joint indices and weights), the record that variant reads,
        // and the per-draw layout it binds in place of @ref draw_layout
        // (the same entries plus the joint-matrix storage buffer). Leave
        // @ref skin_attributes empty for a template that cannot skin: the
        // keyword then changes nothing but the shader defines.
        std::vector<gpu::vertex_attribute> skin_attributes;
        vertex_format skinned_vertex_format{vertex_format::custom};
        gpu::bind_group_layout_descriptor skinned_draw_layout;

        // The pass-owned per-frame layout bound at slot 0, or an invalid
        // handle for materials without one (the per-draw group then
        // takes slot 0).
        gpu::bind_group_layout frame_layout{};

        // Per-material (trailing) bind-group layout: the instance's
        // parameter block and sampled maps. Leave empty for materials
        // with no per-instance GPU resources.
        gpu::bind_group_layout_descriptor material_layout;

        gpu::primitive_topology topology{gpu::primitive_topology::triangles};

        // Whether the depth pre-pass may lay this template's opaque
        // surfaces down through their vertex stage alone (see
        // @ref material::draws_in_depth_prepass). Clear it for a template
        // whose fragment stage decides coverage or depth itself — a
        // @c discard, an alpha test, a @c gl_FragDepth write — since a
        // depth-only pipeline has no fragment stage to run it: the
        // pre-pass then skips its draws, and the scene pass draws them
        // with their ordinary depth-tested, depth-writing variant.
        bool depth_prepass{true};
    };

    struct material_template
    {
        // Creates the bind-group layouts on @p device; no shader is
        // compiled and no pipeline built until the first @ref pipeline
        // request. @p device must outlive the template.
        material_template(gpu::device& device, material_template_descriptor descriptor);
        ~material_template();

        material_template(const material_template&) = delete;
        material_template& operator=(const material_template&) = delete;

        gpu::device& device() const;
        const std::string& name() const;
        const material_template_descriptor& descriptor() const;

        // The pipeline for @p key: served from the cache, or compiled
        // (shader modules per keyword set) and created on first use. A
        // @c depth_only key builds the vertex stage of the same shader
        // set alone.
        gpu::pipeline pipeline(const pipeline_variant_key& key);

        // Whether @ref pipeline for @p key would be a cache hit.
        bool has_variant(const pipeline_variant_key& key) const;

        // Pipelines built so far / keyword sets compiled so far.
        std::size_t variant_count() const;
        std::size_t shader_set_count() const;

        // Layouts and slots shared by every instance. The per-draw group
        // takes slot 1 when the template reserves slot 0 for the per-
        // frame group, else slot 0; the per-material group, when the
        // template has one, always follows the per-draw group.
        gpu::bind_group_layout per_draw_layout() const;
        gpu::bind_group_layout per_material_layout() const;
        bool has_frame_layout() const;
        bool has_material_layout() const;
        uint32_t per_draw_slot() const;
        uint32_t per_material_slot() const;

        // The per-draw layout a variant with @p keywords binds: the
        // skinned layout (created on first use) when @ref skins says so,
        // else @ref per_draw_layout().
        gpu::bind_group_layout per_draw_layout(uint32_t keywords) const;

        // Whether a variant with @p keywords skins: the keyword is set and
        // the template declares
        // @ref material_template_descriptor::skin_attributes.
        bool skins(uint32_t keywords) const;

        // The vertex record a variant with @p keywords reads from slot 0
        // and the narrowest stride it can be bound with (the byte extent
        // of its furthest-reaching slot-0 attribute). A skinning variant
        // reads @ref material_template_descriptor::skinned_vertex_format.
        vertex_format required_vertex_format(uint32_t keywords) const;
        uint32_t min_vertex_stride(uint32_t keywords) const;

        // Every live instance bound to this template, in creation order.
        // Instances register in the @ref material constructor and leave
        // in its destructor, so scene-wide state (the image-based-
        // lighting environment) can be broadcast to all of them.
        const std::vector<material*>& instances() const;

    private:
        friend struct material;
        void register_instance(material* instance);
        void unregister_instance(material* instance);

        struct shader_set
        {
            gpu::shader_module vertex{};
            gpu::shader_module fragment{};
        };

        // Compile (or fetch) the stage pair for @p keywords.
        const shader_set& shaders_for(uint32_t keywords);

        // The vertex layouts a variant with @p keywords binds: the
        // descriptor's, with the tangent attribute dropped when
        // @c has_tangents is off and the skin attributes appended when
        // the variant skins.
        std::vector<gpu::vertex_buffer_layout> vertex_layouts_for(uint32_t keywords) const;

        bool reads_tangents(uint32_t keywords) const;

        gpu::device* m_device{nullptr};
        material_template_descriptor m_descriptor;

        gpu::bind_group_layout m_per_draw_layout{};
        gpu::bind_group_layout m_per_material_layout{};

        // Built the first time a skinning variant (or its per-draw layout)
        // is asked for, so a template nobody skins with creates none.
        // Mutable: @ref per_draw_layout is logically const.
        mutable gpu::bind_group_layout m_skinned_per_draw_layout{};

        std::unordered_map<uint32_t, shader_set> m_shaders;
        std::unordered_map<uint64_t, gpu::pipeline> m_pipelines;
        std::vector<material*> m_instances;
    };
} // namespace rendering_engine
