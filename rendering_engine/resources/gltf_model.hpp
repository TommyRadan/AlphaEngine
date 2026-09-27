// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gltf_model.hpp
 * @brief An imported glTF document on the GPU: its geometry and textures
 *        uploaded through the asset cache and its material descriptions
 *        built into @ref standard_material instances.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <assets/gltf_importer.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct mesh_asset;
    struct renderer;
    struct standard_material;
    struct texture_asset;

    /**
     * @brief The CPU description of a glTF file with the GPU resources made
     *        from it, ready for @c runtime::instantiate_gltf.
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
        // The node tree, skins, animations and material descriptions. The
        // geometry and decoded pixels were released once uploaded; the rest
        // is what instantiation reads.
        assets::gltf_document document;

        // One cached upload per document primitive, index-aligned with
        // @c document.primitives; null where the cache refused it (logged).
        // A mesh in the skinned record format is a skinned primitive.
        std::vector<std::shared_ptr<mesh_asset>> meshes;

        // One cache entry per glTF texture, index-aligned with
        // @c document.textures and uploaded in its colour space. Null when
        // the image could not be decoded.
        std::vector<std::shared_ptr<texture_asset>> textures;

        // One material per glTF material, index-aligned with
        // @c document.materials.
        std::vector<std::shared_ptr<standard_material>> materials;

        // The material for primitives that name none, with glTF's default
        // factors; created only when some primitive needs it.
        std::shared_ptr<standard_material> default_material;

        // The skinning twins of @ref materials (index-aligned; null where no
        // skinned primitive uses the material) and of
        // @ref default_material, built with @c standard_material::set_skinned.
        // A material instance selects one pipeline, so a surface drawn both
        // rigid and skinned needs one of each.
        std::vector<std::shared_ptr<standard_material>> skinned_materials;
        std::shared_ptr<standard_material> skinned_default_material;
    };

    /**
     * @brief A glTF model loading in the background, from
     *        @ref asset_cache::load_gltf_async.
     *
     * Starts @c loading; the cache's @c pump resolves it on the main thread
     * to @c ready, with @ref model filled in, or to @c failed, with
     * @ref error saying why (already logged). Read it from the main thread.
     * The model obeys the same lifetime rules as one
     * @ref asset_cache::load_gltf returns: tear down any nodes instantiated
     * from it before the asset goes (see
     * @c runtime::instantiate_gltf_when_ready for spawning one as soon as it
     * is ready).
     */
    struct gltf_asset
    {
        enum class load_state
        {
            loading, /**< Importing on a worker, or waiting for @c pump. */
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
     * @brief Turns an imported @p document into a @ref gltf_model: uploads
     *        its geometry and textures through @p cache and builds its
     *        materials through @p renderer. Main thread only.
     *
     * Each primitive's geometry is cached under its key, so a file loaded
     * twice shares every upload (a cache hit drops the document's copy). A
     * texture image with a file of its own is keyed on that file, shared
     * with every other loader of it and followed by a debug hot reload; an
     * embedded one on its document key. Each glTF material becomes a
     * @ref standard_material from the renderer's shared standard template
     * (@c renderer::create_standard_material): factors are quantised to
     * the material's 8-bit colours, each slot binds the cached texture of
     * its image in the colour space the slot samples it in, the packed
     * metallic-roughness texture binds as the ORM map, an occlusion texture
     * that is a different image binds as the occlusion map while one that
     * is the same image is read from the packed R, and a material with a
     * packed map but no occlusion texture gets occlusion strength 0 so its
     * R channel is ignored. A material skinned primitives use gets a
     * skinned twin as well. The geometry and pixels are released from the
     * document the model keeps.
     */
    gltf_model upload_gltf(assets::gltf_document document, asset_cache& cache, renderer& renderer);
} // namespace rendering_engine
