// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gltf_material_factory.hpp
 * @brief The production @ref gltf_material_factory: glTF materials as
 *        @ref standard_material instances on the running engine's renderer.
 */

#pragma once

#include <memory>

#include <rendering_engine/assets/gltf_importer.hpp>

namespace rendering_engine
{
    /**
     * @brief Builds each imported material with
     *        @ref renderer::create_standard_material and applies the
     *        description's factors and maps.
     *
     * Factors are quantised to the material's 8-bit colours. Each map binds
     * the description's cached texture (the cache's upload, shared with
     * @ref gltf_model::textures and followed by a debug hot reload) when it
     * has one; otherwise the decoded image is uploaded: the base colour map
     * in the description's colour space, the emissive map as sRGB and the
     * normal / metallic-roughness / occlusion maps as linear. The packed metallic-roughness image binds through
     * @ref standard_material::set_orm_map; an occlusion texture that is a
     * different image binds through @c set_occlusion_map, one that is the
     * same image is read from the packed R, and a material with a packed
     * map but no occlusion texture gets occlusion strength 0 so its R
     * channel is ignored. A description marked skinned builds the instance
     * with @ref standard_material::set_skinned. Every instance shares the
     * renderer's standard template
     * (@ref renderer::create_standard_material). Needs a live
     * renderer (it reaches @ref runtime::current_engine), which is why it
     * lives in its own translation unit apart from the importer.
     */
    struct gltf_standard_material_factory final : gltf_material_factory
    {
        std::shared_ptr<standard_material> create(const gltf_material_description& description) override;
    };
} // namespace rendering_engine
