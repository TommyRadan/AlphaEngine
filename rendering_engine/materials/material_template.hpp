// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file material_template.hpp
 * @brief One material *type*: its shaders, vertex and bind-group layouts,
 *        and the cache of pipeline variants its instances draw with.
 *
 * A @ref material_template is built once per material type from a
 * @ref material_template_descriptor and shared by every @ref material
 * instance of that type. The renderer's material library builds and
 * registers them (@c material_library::create_template): the eight
 * built-in types in @c material_library::init, and any type a game
 * declares the same way, with shaders of its own (asset shaders, see
 * shader_library.hpp). It owns no per-instance state: an instance
 * carries its own parameter block, textures and per-material bind group
 * and points at the template for everything else.
 *
 * **Binding model.** Every template's pipelines share one slot order, so
 * a shader written against it draws in the pass its template names
 * (@ref material_template_descriptor::frame):
 *
 * - set 0, the per-frame set of that pass — for a scene template the
 *   scene pass's view (@c include/per_frame.glsl, @c view_globals), its
 *   lights (@c include/lights.glsl) and shadows (@c include/shadows.glsl);
 *   for a UI template the UI pass's pixel projection;
 * - the per-draw data: the 128-byte @c PerDraw push-constant block (model
 *   and normal matrix, @c include/per_draw.glsl), which every template's
 *   pipelines declare, plus set 1 for what a draw binds beyond it (a
 *   skinned variant's joint palette);
 * - set 2, the per-material set each instance owns (@c PER_MATERIAL_SET in
 *   @c include/per_material.glsl): the parameter block at
 *   @c BINDING_MATERIAL_PARAMS and the sampled maps at the
 *   @c BINDING_MATERIAL_*_MAP numbers.
 *
 * A template with no per-frame set (@ref material_frame::none) shifts the
 * per-draw and per-material sets down by one.
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
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assets/vertex.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/materials/material_parameters.hpp>
#include <rendering_engine/materials/pipeline_variant.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct material;
    struct material_template;

    /**
     * @brief The per-frame set a template's pipelines reserve slot 0 for:
     *        which pass the template's materials draw in.
     */
    enum class material_frame : uint8_t
    {
        /** @brief None; the per-draw group takes slot 0. */
        none,
        /** @brief The scene pass's view, lights and shadows (every 3D template). */
        scene,
        /** @brief The UI pass's pixel-space projection. */
        ui,
    };

    /**
     * @brief Makes an instance of a template; see
     *        @ref material_template_descriptor::instance_factory.
     */
    using material_instance_factory = std::function<std::unique_ptr<material>(std::shared_ptr<material_template>)>;

    /**
     * @brief The factory that makes a @p M over the template it is handed:
     *        a built-in type's @c instance_factory. @p M derives from
     *        @ref material and constructs from the template alone.
     */
    template<typename M>
    material_instance_factory make_instance_factory()
    {
        return [](std::shared_ptr<material_template> tmpl) -> std::unique_ptr<material>
        { return std::make_unique<M>(std::move(tmpl)); };
    }

    // Everything a material type declares up front. The template copies
    // it and derives the rest (layouts, strides) at construction. A game
    // declares a type of its own by filling the shaders, the vertex
    // format, the parameters and textures and the default render state,
    // and hands it to material_library::create_template; the lower-level
    // fields (explicit vertex and bind-group layouts, skinning) serve the
    // built-in types.
    struct material_template_descriptor
    {
        // Bit of the first keyword a template declares of its own
        // (@ref keywords); the engine's keywords (material_keyword) sit
        // below it.
        static constexpr uint32_t first_template_keyword = 16;

        // How many keywords a template can declare of its own.
        static constexpr uint32_t max_template_keywords = 16;

        // Sentinel for @ref tangent_location: the slot-0 layout carries
        // no optional tangent channel.
        static constexpr uint32_t no_tangent_location = UINT32_MAX;

        // The name the material library registers the template under,
        // and the label for log lines.
        std::string name;

        // The two stages by shader-library path (see shader_library.hpp:
        // an asset shader under the content directory's shaders/, or an
        // engine one) plus the defines every variant of this template is
        // compiled with; the keyword defines are appended per variant.
        gpu::shader_variant vertex_shader;
        gpu::shader_variant fragment_shader;

        // Vertex buffer slots in declaration order. Slot 0 is the
        // per-vertex geometry stream every renderable binds; a per-
        // instance stream, if any, follows. Left empty, slot 0 is derived
        // from @ref required_vertex_format: each channel of the record in
        // order at locations 0, 1, 2, ... (position, then colour or uv,
        // normal, tangent), which is what the shader's inputs declare.
        std::vector<gpu::vertex_buffer_layout> vertex_layouts;

        // The record layout the slot-0 attributes read (see
        // @ref vertex_format), and the layout a variant without the
        // @c has_tangents keyword reads instead. Both @c custom when the
        // material declares none; the second defaults to the first for a
        // template without a @ref tangent_location.
        assets::vertex_format required_vertex_format{assets::vertex_format::custom};
        assets::vertex_format vertex_format_without_tangents{assets::vertex_format::custom};

        // Attribute location of the optional tangent channel in slot 0.
        // A variant built without @ref material_keyword::has_tangents
        // drops this attribute from its layout (and reports
        // @ref vertex_format_without_tangents) so a tangent-less record
        // binds without any fetch running past the vertex.
        uint32_t tangent_location{no_tangent_location};

        // Per-draw bind-group layout (the slot after the per-frame group,
        // if any); empty for materials whose per-draw data is pushed
        // (the PerDraw block) or carried in a vertex stream.
        gpu::bind_group_layout_descriptor draw_layout;

        // Skinning support. A template that can skin lists the slot-0
        // attributes a variant with @ref material_keyword::skinned appends
        // (the joint indices and weights), the record that variant reads,
        // and the per-draw layout it binds in place of @ref draw_layout
        // (the joint-matrix storage buffer). Leave
        // @ref skin_attributes empty for a template that cannot skin: the
        // keyword then changes nothing but the shader defines.
        std::vector<gpu::vertex_attribute> skin_attributes;
        assets::vertex_format skinned_vertex_format{assets::vertex_format::custom};
        gpu::bind_group_layout_descriptor skinned_draw_layout;

        // The per-frame set slot 0 is reserved for, which picks the pass
        // the template's materials draw in. The material library hands
        // the template that pass's layout (material_library::frame_layout).
        material_frame frame{material_frame::scene};

        // Per-material (trailing) bind-group layout: the instance's
        // parameter block and sampled maps. Leave empty for materials
        // with no per-instance GPU resources, and for a template that
        // declares @ref parameters or @ref textures, which derives it:
        // the block at @c shader_bindings::material_params, then every
        // texture slot at its binding.
        gpu::bind_group_layout_descriptor material_layout;

        // The members of the per-material parameter block (std140, bound
        // at @c shader_bindings::material_params), and its byte size: 0
        // derives it from the members (the end of the last one, rounded
        // up to 16 bytes). Every instance owns a buffer holding it,
        // written through @ref material::set_float4 and friends.
        std::vector<material_parameter> parameters;
        uint32_t parameter_block_size{0};

        // The per-material sampled textures, bound through
        // @ref material::set_texture.
        std::vector<material_texture_slot> textures;

        // Keywords of the template's own, beyond the engine's
        // (material_keyword): each is a preprocessor symbol defined in the
        // variants that have it set, toggled per instance through
        // @ref material::set_keyword_enabled or by a bound texture slot
        // that names it. At most @ref max_template_keywords; they take the
        // key bits from @ref first_template_keyword up, so the variant
        // cache tells their variants apart like the engine keywords'.
        std::vector<std::string> keywords;

        // The render state a generic instance starts with (a built-in
        // type's constructor passes its own).
        material_params defaults{};

        // What material_library::create_material makes for this template:
        // a built-in type's instance (a phong_material for the phong
        // template), or, left empty, the generic @ref material over the
        // declared parameters and textures.
        material_instance_factory instance_factory;

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
        // Derives what @p descriptor leaves to derivation and creates the
        // bind-group layouts on @p device; no shader is compiled and no
        // pipeline built until the first @ref pipeline request.
        // @p frame_layout is the layout of the per-frame set the
        // descriptor's @c frame names (invalid for material_frame::none).
        // @p device must outlive the template. The material library is
        // what normally builds templates (material_library::create_template),
        // having checked the descriptor with @ref validate.
        material_template(gpu::device& device,
                          material_template_descriptor descriptor,
                          gpu::bind_group_layout frame_layout = {});
        ~material_template();

        material_template(const material_template&) = delete;
        material_template& operator=(const material_template&) = delete;

        // Why @p descriptor cannot build a template (no name, a shader
        // the shader library does not have, no vertex layout and no named
        // vertex format to derive one from, a parameter off its std140
        // alignment or overlapping another, a duplicate name or binding,
        // an unknown slot keyword, too many keywords), or an empty string
        // when it can.
        static std::string validate(const material_template_descriptor& descriptor);

        gpu::device& device() const;
        const std::string& name() const;
        const material_template_descriptor& descriptor() const;

        // The per-frame layout bound at slot 0; invalid for a template
        // with none.
        gpu::bind_group_layout frame_layout() const;

        // The declared parameter named @p name, or null.
        const material_parameter* find_parameter(std::string_view name) const;

        // The index of the declared texture slot named @p name in
        // descriptor().textures, or -1.
        int find_texture(std::string_view name) const;

        // Whether the template declares parameters or textures, which
        // every instance then holds (see material_parameters.hpp).
        bool declares_resources() const;

        // The key bit of the keyword whose define is @p name: an engine
        // keyword's or one the template declares; 0 for neither.
        uint32_t keyword_bit_for(std::string_view name) const;

        // The key bit a bound texture in slot @p slot sets; 0 when the
        // slot names no keyword.
        uint32_t texture_keyword_bit(std::size_t slot) const;

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
        assets::vertex_format required_vertex_format(uint32_t keywords) const;
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

        // The defines a variant with @p keywords compiles @p base with:
        // the engine keywords' and the template's own.
        gpu::shader_defines defines_for(uint32_t keywords, const gpu::shader_defines& base) const;

        gpu::device* m_device{nullptr};
        material_template_descriptor m_descriptor;
        gpu::bind_group_layout m_frame_layout{};

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
