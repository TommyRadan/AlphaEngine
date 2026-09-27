// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>

#include <cstdint>

#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/assets/image.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    struct environment_probe;
    struct texture_asset;

    // Built-in physically-based lit 3D scene material
    // (metallic-roughness workflow). Cook-
    // Torrance specular (GGX distribution, Smith geometry, Schlick-
    // Fresnel) plus a Lambertian diffuse, driven by the per-frame lights
    // UBO the @ref scene_pass uploads at slot 0, binding 2. The modern
    // default surface: more expensive than @ref phong_material but the
    // single biggest step toward physically based visual fidelity.
    //
    // One instance of the shared standard @ref material_template (built
    // by @ref create_template; the renderer keeps one and hands every
    // instance the same). Which maps the shaders sample is a compile-
    // time keyword set derived from the maps that are bound (see
    // @ref material_keyword), so an instance with no normal map draws a
    // variant with no normal-map code, and every instance with the same
    // keywords and base params shares one pipeline.
    //
    // Consumes the position+uv+normal+tangent vertex stream (the tangent
    // feeds the normal-map TBN basis); @ref set_tangents(false) switches
    // to a tangent-less position+uv+normal record and @ref set_skinned
    // to the skinned position+uv+normal+tangent+skin record. Slot layout
    // matches the other 3D materials: the per-frame group at slot 0
    // (camera + lights, owned by the @ref scene_pass) and the per-draw
    // group at slot 1 (model + normal matrix, built by each renderable,
    // plus the joint palette when skinned). The PBR
    // scalars and the optional maps live in the per-material group at
    // slot 2 owned by each instance.
    //
    // Ambient comes from image-based lighting when an @ref environment is
    // attached (diffuse irradiance + prefiltered specular weighted by the
    // split-sum BRDF LUT); otherwise it falls back to the flat
    // @c ambient * albedo term driven by the per-frame ambient light.
    struct standard_material : public material
    {
        // @p tmpl is the shared standard template (see @ref create_template).
        explicit standard_material(std::shared_ptr<material_template> tmpl);
        ~standard_material() override;

        // The template every standard_material shares. @p frame_layout is
        // the per-frame bind-group layout owned by the @ref scene_pass; it
        // must match the layout the pass binds at slot 0 every frame so
        // the pipelines and the runtime bind group agree on slot shape.
        static std::shared_ptr<material_template> create_template(gpu::device& device,
                                                                  gpu::bind_group_layout frame_layout);

        // The descriptor @ref create_template builds from (exposed so a
        // test can construct the template over a fake device).
        static material_template_descriptor template_descriptor(gpu::bind_group_layout frame_layout);

        // Base (albedo) colour. When an albedo map is set the sampled
        // texel modulates this tint (white leaves it unchanged). The
        // alpha channel, times @ref material_params::opacity, is the
        // output alpha.
        void set_base_color(const color& color);

        // Metalness in [0, 1]. 0 is a dielectric (plastic, wood), 1 is a
        // raw metal whose diffuse term vanishes and whose specular tints
        // toward the base colour.
        void set_metalness(float metalness);

        // Perceptual roughness in [0, 1]. 0 is a mirror-smooth surface,
        // 1 is fully diffuse.
        void set_roughness(float roughness);

        // Emissive colour added after shading (unaffected by lights).
        // Black (the default) emits nothing.
        void set_emissive(const color& color);

        // Scalar multiplier on the emissive colour.
        void set_emissive_intensity(float intensity);

        // Each set_*_map takes the colour space the image was authored
        // in and uploads the matching RGBA8 format (@ref gpu::rgba8_format):
        // colour maps default to sRGB so the sampler decodes them to
        // linear before the PBR math, data maps default to linear so their
        // bytes are read as-is. Override only when an asset breaks the
        // convention (e.g. an albedo exported already-linear). Binding or
        // clearing a map changes the keyword set, so the instance rebinds
        // to the matching pipeline variant.
        //
        // Each map can instead be a shared @ref texture_asset from the
        // asset cache (the overloads taking a @c shared_ptr): the instance
        // samples the asset's own upload — in whatever format and colour
        // space it was loaded — rather than a private copy, holds a
        // reference to it, and follows it when its texture is replaced (an
        // asynchronous load resolving, a debug hot reload; see
        // @ref refresh_texture_assets). A null asset clears the map.

        // Bind an albedo (base-colour) texture; its rgb multiplies the
        // base colour and its alpha the output alpha. Replaces any
        // previous map and rebuilds the per-material bind group. Colour
        // data: sRGB by default.
        void set_albedo_map(const image& image, gpu::color_space space = gpu::color_space::srgb);
        void set_albedo_map(std::shared_ptr<texture_asset> texture);
        void clear_albedo_map();

        // Bind a tangent-space normal map; perturbs the shading normal
        // through the per-vertex TBN basis. Sampled only while
        // @ref set_tangents is on (the default). Vector data: linear by
        // default (an sRGB decode would bend the normals).
        void set_normal_map(const image& image, gpu::color_space space = gpu::color_space::linear);
        void set_normal_map(std::shared_ptr<texture_asset> texture);
        void clear_normal_map();

        // Bind a metalness map; its red channel multiplies the metalness
        // scalar. Scalar data: linear by default. Ignored while an ORM
        // map is bound.
        void set_metalness_map(const image& image, gpu::color_space space = gpu::color_space::linear);
        void set_metalness_map(std::shared_ptr<texture_asset> texture);
        void clear_metalness_map();

        // Bind a roughness map; its red channel multiplies the roughness
        // scalar. Scalar data: linear by default. Ignored while an ORM
        // map is bound.
        void set_roughness_map(const image& image, gpu::color_space space = gpu::color_space::linear);
        void set_roughness_map(std::shared_ptr<texture_asset> texture);
        void clear_roughness_map();

        // Bind an ambient-occlusion map; its red channel scales the
        // ambient (indirect) term, weighted by @ref set_occlusion_strength.
        // Scalar data: linear by default. Overrides the packed ORM map's
        // R channel while both are bound.
        void set_occlusion_map(const image& image, gpu::color_space space = gpu::color_space::linear);
        void set_occlusion_map(std::shared_ptr<texture_asset> texture);
        void clear_occlusion_map();

        // Bind one packed occlusion / roughness / metallic map — R
        // occlusion, G roughness, B metallic, the glTF convention — in
        // place of the metalness and roughness maps, which stay bound but
        // unused until it is cleared. Its R channel is the occlusion
        // source unless a separate occlusion map is bound; a packed map
        // whose R carries no occlusion is muted with
        // @ref set_occlusion_strength(0). Data: linear by default.
        void set_orm_map(const image& image, gpu::color_space space = gpu::color_space::linear);
        void set_orm_map(std::shared_ptr<texture_asset> texture);
        void clear_orm_map();

        // How much the occlusion source (the separate map, else the packed
        // map's R) darkens the ambient term: 0 ignores it, 1 (the default)
        // applies it fully.
        void set_occlusion_strength(float strength);

        // Bind an emissive map; multiplied into the emissive term. Colour
        // data: sRGB by default.
        void set_emissive_map(const image& image, gpu::color_space space = gpu::color_space::srgb);
        void set_emissive_map(std::shared_ptr<texture_asset> texture);
        void clear_emissive_map();

        // Attach an image-based-lighting environment. Its irradiance,
        // prefiltered specular (the skybox mip chain) and BRDF LUT replace
        // the flat ambient term. The @ref environment must outlive the
        // material (or be cleared first); only the texture handles are
        // referenced, not owned.
        void set_environment(const environment_probe& env);

        // Detach the environment and revert to the flat ambient term.
        void clear_environment();

        // Scalar multiplier on the image-based ambient term (1 by
        // default). Has no effect on the flat ambient fallback.
        void set_ibl_intensity(float intensity);

        // Whether the vertex stream carries a tangent at location 3
        // (on by default, the position+uv+normal+tangent record every
        // primitive emits). Off, the instance reads a position+uv+normal
        // record, so meshes without tangents draw, and the normal map is
        // not sampled.
        void set_tangents(bool enabled);

        // Whether the instance draws skinned geometry: on, the vertex
        // stage reads the position+uv+normal+tangent+skin record and blends
        // each vertex by the joint palette its renderable binds in the
        // per-draw group (see @ref model::set_joint_matrices). A skinned
        // mesh and a rigid one need separate instances, since the keyword
        // selects the pipeline. Off by default.
        void set_skinned(bool enabled);

        // Whether the shaders currently sample each map (the keyword in
        // effect, not just whether an image was bound).
        bool has_tangents() const;
        bool uses_orm_map() const;

        // Rebuild the per-material bind group when a bound texture asset
        // has had its texture replaced since the group was built (its
        // @c generation moved). The renderer calls this for every
        // standard instance at the top of each frame, once the device
        // frame is open and before any pass binds the group; returns
        // whether it rebuilt.
        bool refresh_texture_assets();

    protected:
        // Opacity lives in the parameter block, so a base params change
        // re-uploads it.
        void on_params_changed() override;

    private:
        // Derive the keyword set from the bound maps and the tangent
        // flag, and rebind the variant if it changed.
        void update_keywords();

        // (Re)create the per-material bind group against the current map
        // handles, then push the latest params into the UBO.
        void rebuild_bind_group();

        // Push the PBR scalars, opacity and the IBL flags into the per-
        // material UBO.
        void upload_params();

        color m_base_color{255, 255, 255, 255};
        color m_emissive{0, 0, 0, 255};
        float m_metalness{0.0f};
        float m_roughness{1.0f};
        float m_emissive_intensity{1.0f};
        float m_occlusion_strength{1.0f};
        float m_ibl_intensity{1.0f};
        bool m_tangents{true};
        bool m_skinned{false};

        // One bound map: a private upload the instance owns, or a shared
        // cache texture it samples through the asset's current handle.
        struct map_slot
        {
            gpu::texture owned{};
            std::shared_ptr<texture_asset> asset;
            // The asset's generation the bind group was built against.
            uint64_t generation{0};

            gpu::texture handle() const;
            bool bound() const;
        };

        // Drop whatever @p slot holds (freeing a private upload).
        void release_slot(map_slot& slot);
        void set_slot_image(map_slot& slot, const image& image, gpu::color_space space);
        void set_slot_asset(map_slot& slot, std::shared_ptr<texture_asset> texture);
        void clear_slot(map_slot& slot);

        gpu::buffer m_material_ubo{};
        map_slot m_albedo_map{};
        map_slot m_normal_map{};
        map_slot m_metalness_map{};
        map_slot m_roughness_map{};
        map_slot m_emissive_map{};
        map_slot m_occlusion_map{};
        map_slot m_orm_map{};

        // Image-based-lighting source, or null for the flat ambient
        // fallback. Non-owning: the caller keeps the @ref environment
        // alive. Only its texture handles are bound into the per-material
        // group; the @c iblParams flag in the UBO gates the shader path.
        const environment_probe* m_environment{nullptr};
    };
} // namespace rendering_engine
