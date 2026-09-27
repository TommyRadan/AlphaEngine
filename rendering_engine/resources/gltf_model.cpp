// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/resources/gltf_model.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <assets/color.hpp>
#include <assets/vertex.hpp>
#include <core/log.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>
#include <rendering_engine/resources/texture_asset.hpp>

namespace rendering_engine
{
    namespace
    {
        // glTF factors are linear floats in [0, 1]; standard_material takes
        // 8-bit colours it divides by 255 without any sRGB decode, so a
        // straight quantisation preserves the value.
        uint8_t quantize(float value)
        {
            return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        assets::color to_color(const core::math::vec4& factor)
        {
            return assets::color{quantize(factor.x), quantize(factor.y), quantize(factor.z), quantize(factor.w)};
        }

        assets::color to_color(const core::math::vec3& factor)
        {
            return assets::color{quantize(factor.x), quantize(factor.y), quantize(factor.z), 255};
        }

        // The cache texture for image @p index of @p document in @p space, or
        // null when it has no decoded pixels (the importer warned). An image
        // with a file of its own is keyed on that file's path — shared with
        // every other loader of it, and followed by a debug hot reload; an
        // embedded image (GLB chunk or data URI) on its document key.
        std::shared_ptr<texture_asset> image_texture(const assets::gltf_document& document,
                                                     asset_cache& cache,
                                                     std::size_t index,
                                                     assets::color_space space)
        {
            if (index >= document.images.size() || !document.images[index].pixels.has_value())
            {
                return nullptr;
            }
            const assets::gltf_image& image = document.images[index];
            if (!image.file.empty())
            {
                return cache.adopt_texture_image(image.file, *image.pixels, space);
            }
            return cache.load_texture_from_image(image.key, *image.pixels, space);
        }

        // The image texture @p texture of @p document samples, or gltf_npos.
        std::size_t texture_image(const assets::gltf_document& document, std::size_t texture)
        {
            return texture < document.textures.size() ? document.textures[texture].image : assets::gltf_npos;
        }

        // The cache textures a material's slots bind, each in the colour
        // space its slot samples it in; null for an empty slot.
        struct material_maps
        {
            std::shared_ptr<texture_asset> base_color;
            std::shared_ptr<texture_asset> metallic_roughness;
            std::shared_ptr<texture_asset> normal;
            std::shared_ptr<texture_asset> emissive;
            std::shared_ptr<texture_asset> occlusion;

            // The occlusion texture samples the packed metallic-roughness
            // image, whose R channel then is the occlusion.
            bool occlusion_is_packed{false};
        };

        material_maps
        resolve_maps(const assets::gltf_document& document, asset_cache& cache, const assets::gltf_material& material)
        {
            const auto slot = [&](std::size_t texture, assets::color_space space)
            { return image_texture(document, cache, texture_image(document, texture), space); };

            material_maps maps;
            maps.base_color = slot(material.base_color_texture, material.base_color_space);
            maps.metallic_roughness = slot(material.metallic_roughness_texture, assets::color_space::linear);
            maps.normal = slot(material.normal_texture, assets::color_space::linear);
            maps.emissive = slot(material.emissive_texture, assets::color_space::srgb);
            maps.occlusion = slot(material.occlusion_texture, assets::color_space::linear);
            const std::size_t occlusion_image = texture_image(document, material.occlusion_texture);
            maps.occlusion_is_packed = occlusion_image != assets::gltf_npos &&
                                       occlusion_image == texture_image(document, material.metallic_roughness_texture);
            return maps;
        }

        // The standard_material @p description describes, drawn through the
        // skinning variant when @p skinned.
        std::shared_ptr<standard_material> create_material(renderer& renderer,
                                                           const assets::gltf_material& description,
                                                           const material_maps& maps,
                                                           bool skinned)
        {
            std::shared_ptr<standard_material> material = renderer.create_standard_material();
            material->set_skinned(skinned);
            material->set_base_color(to_color(description.base_color_factor));
            material->set_metalness(std::clamp(description.metallic_factor, 0.0f, 1.0f));
            material->set_roughness(std::clamp(description.roughness_factor, 0.0f, 1.0f));
            material->set_emissive(to_color(description.emissive_factor));
            material->set_emissive_intensity(std::max(description.emissive_strength, 0.0f));

            // Each map binds the cache's texture, shared with the model's
            // textures and followed by a debug hot reload.
            if (maps.base_color != nullptr)
            {
                material->set_albedo_map(maps.base_color);
            }
            if (maps.normal != nullptr)
            {
                material->set_normal_map(maps.normal);
            }
            if (maps.emissive != nullptr)
            {
                material->set_emissive_map(maps.emissive);
            }

            // glTF's packed R occlusion / G roughness / B metallic layout is
            // the material's ORM convention, so the texture binds as-is. A
            // separate occlusion texture binds beside it and overrides the
            // packed R; one that is the very same image needs no second
            // binding, the packed R already is the occlusion source.
            const bool has_packed = maps.metallic_roughness != nullptr;
            const bool has_occlusion = maps.occlusion != nullptr;
            if (has_packed)
            {
                material->set_orm_map(maps.metallic_roughness);
            }
            if (has_occlusion && !maps.occlusion_is_packed)
            {
                material->set_occlusion_map(maps.occlusion);
            }
            // Without an occlusion texture the packed map's R channel holds
            // whatever the author left there (often, not always, white), so
            // mute the occlusion term rather than copy the image with R forced
            // to 1. A material with no packed map keeps the default strength.
            if (has_occlusion)
            {
                material->set_occlusion_strength(std::clamp(description.occlusion_strength, 0.0f, 1.0f));
            }
            else if (has_packed)
            {
                material->set_occlusion_strength(0.0f);
            }
            return material;
        }

        bool is_skinned(const std::shared_ptr<mesh_asset>& mesh)
        {
            return mesh != nullptr && mesh->format == assets::vertex_format::position_uv_normal_tangent_skin;
        }
    } // namespace

    gltf_model upload_gltf(assets::gltf_document document, asset_cache& cache, renderer& renderer)
    {
        gltf_model model;

        // Every primitive through the cache. The builder only runs on a
        // miss; a hit shares the upload of an earlier load of this file.
        // Either way the document's copy of the geometry goes.
        model.meshes.resize(document.primitives.size());
        std::size_t uploaded = 0;
        for (std::size_t p = 0; p < document.primitives.size(); ++p)
        {
            assets::gltf_primitive& primitive = document.primitives[p];
            model.meshes[p] = cache.get_or_create_mesh(
                primitive.key, [&primitive]() -> assets::mesh_data { return std::move(primitive.geometry); });
            primitive.geometry = assets::mesh_data{};
            uploaded += model.meshes[p] != nullptr ? 1 : 0;
        }

        // One cache texture per glTF texture, in the colour space the
        // materials sampling it expect.
        model.textures.resize(document.textures.size());
        for (std::size_t t = 0; t < document.textures.size(); ++t)
        {
            const assets::gltf_texture& texture = document.textures[t];
            model.textures[t] = image_texture(document, cache, texture.image, texture.space);
            if (model.textures[t] == nullptr && texture.image < document.images.size() &&
                !document.images[texture.image].file.empty())
            {
                LOG_WRN("gltf: texture %zu of '%s' could not be loaded", t, document.path.string().c_str());
            }
        }

        // One material per glTF material, plus the shared default when a
        // primitive names none, and a skinned twin of each of those a
        // skinned primitive draws with. The resolved maps are reused for
        // the twins.
        std::vector<material_maps> maps;
        maps.reserve(document.materials.size());
        model.materials.reserve(document.materials.size());
        for (const assets::gltf_material& material : document.materials)
        {
            maps.push_back(resolve_maps(document, cache, material));
            model.materials.push_back(create_material(renderer, material, maps.back(), false));
        }

        bool needs_default = false;
        bool skinned_default = false;
        std::vector<bool> skinned_use(document.materials.size(), false);
        for (std::size_t p = 0; p < document.primitives.size(); ++p)
        {
            if (model.meshes[p] == nullptr)
            {
                continue;
            }
            const std::size_t material = document.primitives[p].material;
            needs_default = needs_default || material == assets::gltf_npos;
            if (!is_skinned(model.meshes[p]))
            {
                continue;
            }
            if (material == assets::gltf_npos)
            {
                skinned_default = true;
            }
            else if (material < skinned_use.size())
            {
                skinned_use[material] = true;
            }
        }

        // glTF's default material: white, fully metallic, fully rough.
        assets::gltf_material default_description;
        default_description.name = "gltf default";
        if (needs_default)
        {
            model.default_material = create_material(renderer, default_description, material_maps{}, false);
        }
        model.skinned_materials.assign(document.materials.size(), nullptr);
        for (std::size_t m = 0; m < document.materials.size(); ++m)
        {
            if (skinned_use[m])
            {
                model.skinned_materials[m] = create_material(renderer, document.materials[m], maps[m], true);
            }
        }
        if (skinned_default)
        {
            model.skinned_default_material = create_material(renderer, default_description, material_maps{}, true);
        }

        LOG_INF("gltf: loaded '%s' (%zu nodes, %zu primitives, %zu materials, %zu textures)",
                document.path.generic_string().c_str(),
                document.nodes.size(),
                uploaded,
                model.materials.size(),
                model.textures.size());

        // The pixels live on in the cache's uploads; the model keeps the
        // rest of the document for instantiation.
        for (assets::gltf_image& image : document.images)
        {
            image.pixels.reset();
        }
        model.document = std::move(document);
        return model;
    }
} // namespace rendering_engine
