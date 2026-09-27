// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file scene_assets.hpp
 * @brief How a scene file names the assets its components draw with — mesh
 *        references and material descriptions — and how a load turns them
 *        back into live assets through the asset cache.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <core/math/vec4.hpp>
#include <rendering_engine/materials/pipeline_variant.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;
} // namespace rendering_engine

namespace runtime
{
    /**
     * @brief How a scene file names the file a VFS canonical key
     *        (@c core::vfs::canonical_key) identifies: the mount-relative
     *        path that reaches it (see @c core::vfs::virtual_path), so the
     *        file does not depend on where the asset root sits on the
     *        machine that saved it, or the key itself when no mount holds it.
     */
    std::string file_reference(const std::string& canonical_key);

    // --- Meshes -----------------------------------------------------------

    /**
     * @brief The reference a scene file stores for @p mesh, or an empty
     *        string for an asset the cache never keyed.
     *
     * The asset's cache key (@ref rendering_engine::mesh_asset::key), except
     * that the file inside an imported model's key —
     * @c "gltf:<canonical path>#mesh<i>/prim<j>" — is re-spelled through
     * @ref file_reference (@c "gltf:models/duck.glb#mesh0/prim0").
     */
    std::string mesh_reference(const rendering_engine::mesh_asset& mesh);

    /**
     * @brief The mesh @p reference names, or @c nullptr (with a warning)
     *        when it cannot be found or rebuilt.
     *
     * A mesh still alive in the asset cache is shared. Otherwise a
     * @c "gltf:" reference imports its file (held for the rest of the scene
     * load, see @c keep_alive_while_loading, so its other meshes resolve from
     * the same import) and any other reference goes to the resolver
     * registered for the longest matching prefix
     * (@ref register_mesh_resolver). Main-thread-only; needs a running
     * engine.
     */
    std::shared_ptr<rendering_engine::mesh_asset> resolve_mesh_reference(const std::string& reference);

    /** @brief Rebuilds the mesh a reference names; returns @c nullptr when it cannot. */
    using mesh_resolver = std::function<std::shared_ptr<rendering_engine::mesh_asset>(const std::string& reference)>;

    /**
     * @brief Teaches @ref resolve_mesh_reference to rebuild the meshes whose
     *        reference starts with @p prefix — procedural geometry cached
     *        under a structural key, typically — when the cache no longer
     *        holds them. Replaces a resolver registered for the same prefix.
     */
    void register_mesh_resolver(std::string prefix, mesh_resolver resolver);

    // --- Materials --------------------------------------------------------

    /**
     * @brief Everything that makes one @ref rendering_engine::standard_material,
     *        as data: the shared parameter surface, the PBR values and the
     *        maps as VFS paths (empty for none).
     *
     * Colours are linear RGBA floats in [0, 1]; the material stores them as
     * 8-bit channels. Maps are decoded in the colour space their slot
     * implies (albedo and emissive sRGB, the others linear).
     */
    struct standard_material_description
    {
        rendering_engine::material_params params{};
        core::math::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
        float metalness{0.0f};
        float roughness{1.0f};
        core::math::vec4 emissive{0.0f, 0.0f, 0.0f, 1.0f};
        float emissive_intensity{1.0f};
        float occlusion_strength{1.0f};
        float ibl_intensity{1.0f};
        bool tangents{true};
        bool skinned{false};
        std::string albedo_map;
        std::string normal_map;
        std::string metalness_map;
        std::string roughness_map;
        std::string occlusion_map;
        std::string orm_map;
        std::string emissive_map;
    };

    /**
     * @brief The standard material @p description describes: a live one made
     *        from an identical description is shared, otherwise a new one is
     *        built (a map that fails to load is warned about and left
     *        unbound). Freed when the last holder lets go.
     *
     * Needs a running renderer. A shared material is one object: changing it
     * at runtime changes it for every holder, as with any shared asset.
     */
    std::shared_ptr<rendering_engine::material>
    acquire_standard_material(const standard_material_description& description);

    /**
     * @brief The description @p material was built from by
     *        @ref acquire_standard_material, or @c nullptr for a material
     *        made any other way (whose colours, scalars and maps cannot be
     *        read back).
     */
    const standard_material_description* find_material_description(const rendering_engine::material& material);
} // namespace runtime
