// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gltf_importer.hpp
 * @brief glTF 2.0 importer: meshes (static and skinned), PBR materials, the
 *        node hierarchy, skins and animations of a .gltf / .glb file, routed
 *        through the asset cache.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/assets/image.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/assets/texture_asset.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <runtime/animation/animation_clip.hpp>
#include <runtime/animation/skeleton.hpp>

namespace rendering_engine
{
    struct asset_cache;

    /** @brief "No index": a primitive without a material, a node without a parent. */
    inline constexpr std::size_t gltf_npos = static_cast<std::size_t>(-1);

    /** @brief Knobs for @ref load_gltf. */
    struct gltf_import_options
    {
        // Colour space the base-colour (albedo) images were authored in.
        // glTF mandates sRGB; override only for an asset that breaks the
        // spec. Emissive maps are always sRGB and the normal / metallic-
        // roughness / occlusion data maps always linear.
        gpu::color_space base_color_space{gpu::color_space::srgb};

        // Derive tangents (via @ref generate_tangents) for primitives that
        // ship none. Off, such primitives get a constant placeholder
        // tangent, which is only acceptable when no normal map reads it.
        bool generate_missing_tangents{true};
    };

    /**
     * @brief One drawable piece of a glTF mesh: a cached
     *        @ref vertex_position_uv_normal_tangent upload (or, for a
     *        primitive with JOINTS_0 / WEIGHTS_0, a
     *        @ref vertex_position_uv_normal_tangent_skin one) plus the
     *        material it is drawn with.
     */
    struct gltf_mesh_primitive
    {
        // Shared geometry, keyed in the asset cache on the file's canonical
        // path and the primitive's mesh / primitive indices.
        std::shared_ptr<mesh_asset> mesh;

        // Index into @ref gltf_model::materials, or @ref gltf_npos when the
        // primitive names none (draw it with @ref gltf_model::default_material).
        // The object-space box is @c mesh->bounds: the POSITION accessor's
        // min / max when the file supplies them (the spec requires it), else
        // scanned from the positions by the cache at upload.
        std::size_t material_index{gltf_npos};

        // Whether @ref mesh carries joint indices and weights (the skinned
        // record). A node with a skin draws such a primitive with
        // @ref gltf_model::skinned_materials; anywhere else it draws rigid,
        // in its bind pose.
        bool skinned{false};
    };

    /** @brief One glTF node: a name, a local TRS pose and its links. */
    struct gltf_node
    {
        std::string name;

        // Index into @ref gltf_model::nodes, or @ref gltf_npos for a root.
        std::size_t parent{gltf_npos};

        // Indices into @ref gltf_model::nodes, in file order.
        std::vector<std::size_t> children;

        // Local pose in the file's own coordinate system (+Y up, right-
        // handed); a node authored as a matrix is decomposed into TRS.
        core::math::vec3 translation{0.0f, 0.0f, 0.0f};
        core::math::quat rotation{};
        core::math::vec3 scale{1.0f, 1.0f, 1.0f};

        // Indices into @ref gltf_model::primitives drawn at this node.
        std::vector<std::size_t> primitives;

        // Index into @ref gltf_model::node_skeleton's skins deforming this
        // node's skinned primitives, or @ref gltf_npos.
        std::size_t skin{gltf_npos};
    };

    /**
     * @brief Everything the importer knows about one glTF material, in the
     *        engine's terms, handed to a @ref gltf_material_factory.
     *
     * The factors are glTF's (linear, unquantised). The map pointers are
     * non-owning and valid only for the duration of the factory call: the
     * importer decodes each image once and frees everything when the load
     * returns. The packed metallic-roughness texture passes through as
     * decoded — glTF's R occlusion / G roughness / B metallic layout is the
     * ORM map @ref standard_material takes — so nothing is split.
     */
    struct gltf_material_description
    {
        std::string name;

        core::math::vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic_factor{1.0f};
        float roughness_factor{1.0f};
        core::math::vec3 emissive_factor{0.0f, 0.0f, 0.0f};

        // KHR_materials_emissive_strength multiplier; 1 when absent.
        float emissive_strength{1.0f};

        // Colour space to upload @ref base_color_map in (from the import
        // options); the other maps' spaces are fixed by the glTF spec.
        gpu::color_space base_color_space{gpu::color_space::srgb};

        // Decoded maps, or nullptr when the material has none.
        const image* base_color_map{nullptr};
        const image* normal_map{nullptr};

        // The packed metallicRoughnessTexture: G roughness, B metallic. Its
        // R channel is occlusion only when @ref occlusion_map points at the
        // same image (glTF's usual ORM packing).
        const image* metallic_roughness_map{nullptr};

        // The occlusionTexture (R channel), which may be the very image
        // @ref metallic_roughness_map names; nullptr without one.
        const image* occlusion_map{nullptr};

        const image* emissive_map{nullptr};

        // occlusionTexture.strength; 1 when @ref occlusion_map is null.
        float occlusion_strength{1.0f};

        // The same maps as cached textures, each uploaded in the colour
        // space its slot samples it in (base colour in @ref
        // base_color_space, emissive sRGB, the rest linear): what a factory
        // binds so the material shares the cache's upload — and follows a
        // debug hot reload of an external image file — instead of
        // uploading a copy of the decoded image. Null exactly where the
        // matching image pointer is; the occlusion texture is the very
        // metallic-roughness one when the two slots name one image.
        std::shared_ptr<texture_asset> base_color_texture;
        std::shared_ptr<texture_asset> normal_texture;
        std::shared_ptr<texture_asset> metallic_roughness_texture;
        std::shared_ptr<texture_asset> occlusion_texture;
        std::shared_ptr<texture_asset> emissive_texture;

        // Build the instance for skinned primitives
        // (@ref standard_material::set_skinned): the same surface, drawn
        // through the skinning variant.
        bool skinned{false};
    };

    /**
     * @brief Turns a @ref gltf_material_description into a material.
     *
     * A real @ref standard_material can only be built against the live
     * renderer, while the importer's geometry and texture paths need only
     * the asset cache. Routing material creation through this interface
     * keeps that dependency out of the importer: the engine-backed loaders
     * pass @ref gltf_standard_material_factory.
     *
     * The material comes back as a @c shared_ptr on purpose: a shared_ptr
     * captures its deleter where the object is created (inside the
     * renderer-side factory), so the importer never instantiates @c delete
     * on a @ref standard_material. With a @c unique_ptr it would, and the
     * undefined-behaviour sanitizer's vptr check then emits a static
     * reference to the material's typeinfo, which only links when
     * @c standard_material.cpp is part of the binary.
     */
    struct gltf_material_factory
    {
        virtual ~gltf_material_factory() = default;

        /** @brief Builds the material for @p description; may return null. */
        virtual std::shared_ptr<standard_material> create(const gltf_material_description& description) = 0;
    };

    /**
     * @brief The imported model: cached primitives, materials, the node
     *        tree and its animation data, ready for
     *        @ref runtime::instantiate_gltf.
     *
     * Holds the only references to its materials (shared handles, so they
     * are destroyed wherever the last one drops — normally here) and shares
     * the meshes / textures with the cache. The materials must not outlive
     * the renderer, and the model must outlive any scene nodes instantiated
     * from it (their mesh components draw with raw material pointers): tear
     * the nodes down first, then the model, before the engine quits.
     */
    struct gltf_model
    {
        // Every node in the file, index-aligned with the glTF node array.
        std::vector<gltf_node> nodes;

        // The nodes to instantiate: the default scene's roots (or the first
        // scene's, or every parentless node when the file declares no scene).
        std::vector<std::size_t> root_nodes;

        // Every TRIANGLES primitive that imported; other topologies are
        // skipped with a warning and never referenced from a node.
        std::vector<gltf_mesh_primitive> primitives;

        // One material per glTF material, index-aligned with the file's
        // material array. Entries are whatever the factory returned.
        std::vector<std::shared_ptr<standard_material>> materials;

        // The material for primitives that name none; created (through the
        // factory, with glTF's defaults) only when some primitive needs it.
        std::shared_ptr<standard_material> default_material;

        // The skinning twins of @ref materials (index-aligned; null where no
        // skinned primitive uses the material) and of
        // @ref default_material, created with
        // @ref gltf_material_description::skinned set. A material instance
        // selects one pipeline, so a surface drawn both rigid and skinned
        // needs one of each.
        std::vector<std::shared_ptr<standard_material>> skinned_materials;
        std::shared_ptr<standard_material> skinned_default_material;

        // The skeleton over every node of the file, index-aligned with
        // @ref nodes (joint k is node k, its bind pose the node's TRS) and
        // carrying one skin per glTF skin (the palette in the skin's joint
        // order, with its inverse bind matrices). Null when the file has
        // neither skins nor animations.
        std::shared_ptr<const runtime::animation::skeleton> node_skeleton;

        // One clip per glTF animation, in file order, whose tracks drive
        // @ref node_skeleton joints (so node indices): translation, rotation
        // and scale channels with their step / linear / cubic-spline
        // sampling. Morph-target weight channels are not imported.
        std::vector<std::shared_ptr<const runtime::animation::animation_clip>> animations;

        // One cache entry per glTF texture, index-aligned with the file's
        // texture array and uploaded in the colour space the material that
        // samples it expects. Null when the image could not be decoded.
        std::vector<std::shared_ptr<texture_asset>> textures;
    };

    /**
     * @brief A glTF model loading in the background, from
     *        @ref asset_cache::load_gltf_async.
     *
     * Starts @c loading; the cache's @c pump resolves it on the main thread
     * to @c ready, with @ref model filled in, or to @c failed, with
     * @ref error saying why (already logged). Read it from the main thread.
     * The model obeys the same lifetime rules as one @ref load_gltf
     * returns: tear down any nodes instantiated from it before the asset
     * goes (see @c runtime::instantiate_gltf_when_ready for spawning one
     * as soon as it is ready).
     */
    struct gltf_asset
    {
        enum class load_state
        {
            loading, /**< Parsing and decoding on a worker, or waiting for @c pump. */
            ready,   /**< @ref model is complete. */
            failed,  /**< The file could not be loaded; see @ref error. */
        };

        load_state state{load_state::loading};

        // The imported model; empty until @ref state is ready.
        gltf_model model;

        // Why the load failed; empty otherwise.
        std::string error;

        bool is_ready() const noexcept
        {
            return state == load_state::ready;
        }
    };

    /**
     * @brief The CPU half of an import, from @ref begin_gltf_import to
     *        @ref finish_gltf_import: the parsed file with its buffers, the
     *        decoded images, the prebuilt geometry and the node, skeleton
     *        and animation data. Opaque.
     */
    struct gltf_import;

    /** @brief Frees a @ref gltf_import (where its type is complete). */
    struct gltf_import_deleter
    {
        void operator()(gltf_import* import) const noexcept;
    };

    using gltf_import_ptr = std::unique_ptr<gltf_import, gltf_import_deleter>;

    /**
     * @brief The device-free half of @ref load_gltf: parses and validates
     *        the file (and its buffers) through the VFS and builds the node
     *        tree, the skeleton and the animation clips.
     *
     * With @p prebuild it also decodes every image a texture references and
     * builds every primitive's geometry now, which is how
     * @ref asset_cache::load_gltf_async runs it on a worker; without it both
     * are left to @ref finish_gltf_import, which then builds only the
     * geometry the cache does not already hold (the synchronous path).
     * Touches neither the device nor the asset cache, so it is safe on any
     * thread. Throws @c std::runtime_error (after logging) when the file
     * cannot be parsed, its buffers cannot be loaded, or it fails
     * validation.
     */
    gltf_import_ptr
    begin_gltf_import(const std::filesystem::path& path, const gltf_import_options& options = {}, bool prebuild = true);

    /**
     * @brief The main-thread half of @ref load_gltf: uploads the geometry
     *        and textures through @p cache, builds the materials through
     *        @p materials and returns the finished model. @p import is
     *        spent afterwards.
     */
    gltf_model finish_gltf_import(gltf_import& import, asset_cache& cache, gltf_material_factory& materials);

    /**
     * @brief Loads the .gltf / .glb at @p path.
     *
     * Handles external buffer and image files (resolved relative to the glTF
     * file), GLB binary chunks and base64 data URIs. Each TRIANGLES primitive
     * becomes a @ref vertex_position_uv_normal_tangent mesh — flat normals are
     * generated when the file has none (its tangents are then ignored, as the
     * spec requires), tangents are taken from the file or derived; a primitive
     * with JOINTS_0 / WEIGHTS_0 becomes a
     * @ref vertex_position_uv_normal_tangent_skin mesh, its weights
     * renormalised to sum to 1 — cached in
     * @p cache under @c "gltf:<canonical path>#mesh<i>/prim<j>", so loading the
     * same file twice shares every upload. Materials go through @p materials
     * (twice for a material skinned primitives use: rigid and skinned);
     * textures are loaded into @p cache. Skins and animations become
     * @ref gltf_model::node_skeleton and @ref gltf_model::animations. Throws @c std::runtime_error (after
     * logging) when the file cannot be parsed, its buffers cannot be loaded, or
     * it fails validation; an undecodable image is warned about and skipped.
     * @ref begin_gltf_import followed by @ref finish_gltf_import on the
     * calling thread; @ref asset_cache::load_gltf_async runs the first half
     * on the worker pool instead.
     */
    gltf_model load_gltf(const std::filesystem::path& path,
                         asset_cache& cache,
                         gltf_material_factory& materials,
                         const gltf_import_options& options = {});

    /**
     * @brief Loads the .gltf / .glb at @p path against the running engine:
     *        its asset cache and a @ref gltf_standard_material_factory.
     *
     * Defined in gltf_material_factory.cpp, the one importer translation
     * unit that reaches the live renderer.
     */
    gltf_model load_gltf(const std::filesystem::path& path, const gltf_import_options& options = {});

    /**
     * @brief Starts loading the .gltf / .glb at @p path in the background
     *        against the running engine's asset cache (and the
     *        @ref gltf_standard_material_factory the engine installs in it).
     *        See @ref asset_cache::load_gltf_async.
     */
    std::shared_ptr<gltf_asset> load_gltf_async(const std::filesystem::path& path,
                                                const gltf_import_options& options = {});
} // namespace rendering_engine
