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
 * @file gltf_importer.hpp
 * @brief glTF 2.0 importer: static meshes, PBR materials and the node
 *        hierarchy of a .gltf / .glb file, routed through the asset cache.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/assets/texture_asset.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/util/image.hpp>

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
     *        @ref vertex_position_uv_normal_tangent upload plus the material
     *        it is drawn with.
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
    };

    /**
     * @brief Everything the importer knows about one glTF material, in the
     *        engine's terms, handed to a @ref gltf_material_factory.
     *
     * The factors are glTF's (linear, unquantised). The map pointers are
     * non-owning and valid only for the duration of the factory call: the
     * importer decodes each image once, splits the packed metallic-roughness
     * texture into the two single-channel maps @ref standard_material reads,
     * and frees everything when the load returns.
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

        // Decoded maps, or nullptr when the material has none. The metallic
        // and roughness maps carry the glTF texture's B and G channels
        // respectively, replicated into R (the channel the material samples).
        const util::image* base_color_map{nullptr};
        const util::image* normal_map{nullptr};
        const util::image* metallic_map{nullptr};
        const util::image* roughness_map{nullptr};
        const util::image* emissive_map{nullptr};
    };

    /**
     * @brief Turns a @ref gltf_material_description into a material.
     *
     * The importer's geometry and texture paths need only the asset cache and
     * its (fake-able) device, but a real @ref standard_material can only be
     * built against the live renderer. Routing material creation through
     * this interface keeps that dependency out of the importer: production
     * passes @ref gltf_standard_material_factory, tests pass a recorder.
     *
     * The material comes back as a @c shared_ptr on purpose: a shared_ptr
     * captures its deleter where the object is created (inside the
     * renderer-side factory), so the importer and the headless test binary
     * never instantiate @c delete on a @ref standard_material. With a
     * @c unique_ptr they would, and the undefined-behaviour sanitizer's vptr
     * check then emits a static reference to the material's typeinfo, which
     * only links when @c standard_material.cpp is part of the binary.
     */
    struct gltf_material_factory
    {
        virtual ~gltf_material_factory() = default;

        /** @brief Builds the material for @p description; may return null. */
        virtual std::shared_ptr<standard_material> create(const gltf_material_description& description) = 0;
    };

    /**
     * @brief The imported model: cached primitives, materials and the node
     *        tree, ready for @ref runtime::instantiate_gltf.
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

        // One cache entry per glTF texture, index-aligned with the file's
        // texture array and uploaded in the colour space the material that
        // samples it expects. Null when the image could not be decoded.
        std::vector<std::shared_ptr<texture_asset>> textures;
    };

    /**
     * @brief Loads the .gltf / .glb at @p path.
     *
     * Handles external buffer and image files (resolved relative to the glTF
     * file), GLB binary chunks and base64 data URIs. Each TRIANGLES primitive
     * becomes a @ref vertex_position_uv_normal_tangent mesh — flat normals are
     * generated when the file has none (its tangents are then ignored, as the
     * spec requires), tangents are taken from the file or derived — cached in
     * @p cache under @c "gltf:<canonical path>#mesh<i>/prim<j>", so loading the
     * same file twice shares every upload. Materials go through @p materials;
     * textures are loaded into @p cache. Throws @c std::runtime_error (after
     * logging) when the file cannot be parsed, its buffers cannot be loaded, or
     * it fails validation; an undecodable image is warned about and skipped.
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
} // namespace rendering_engine
