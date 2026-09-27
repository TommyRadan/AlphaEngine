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

#include <runtime/scene_assets.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/vfs/vfs.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/cache_key.hpp>
#include <rendering_engine/assets/gltf_importer.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/image.hpp>
#include <runtime/engine.hpp>
#include <runtime/scene_serializer.hpp>

namespace runtime
{
    namespace
    {
        constexpr std::string_view k_gltf_prefix = "gltf:";

        std::map<std::string, mesh_resolver, std::less<>>& mesh_resolvers()
        {
            static std::map<std::string, mesh_resolver, std::less<>> resolvers;
            return resolvers;
        }

        // Splits "gltf:<file>#<fragment>"; false for anything else.
        bool split_gltf(const std::string& text, std::string& file, std::string& fragment)
        {
            if (!text.starts_with(k_gltf_prefix))
            {
                return false;
            }
            const std::size_t hash = text.rfind('#');
            if (hash == std::string::npos || hash <= k_gltf_prefix.size())
            {
                return false;
            }
            file = text.substr(k_gltf_prefix.size(), hash - k_gltf_prefix.size());
            fragment = text.substr(hash + 1);
            return true;
        }

        std::shared_ptr<rendering_engine::mesh_asset> resolve_gltf(const std::string& file, const std::string& fragment)
        {
            rendering_engine::asset_cache& cache = *current_engine().assets;
            const std::filesystem::path path = core::platform::utf8_path(file);
            const std::string key =
                std::string{k_gltf_prefix} + core::default_vfs().canonical_key(path) + "#" + fragment;
            if (auto live = cache.find_mesh(key))
            {
                return live;
            }

            // Import the model; its meshes land in the cache under their keys.
            // It is held for the rest of the scene load, so the document's
            // other meshes from this file resolve from this one import.
            std::shared_ptr<rendering_engine::gltf_model> model;
            try
            {
                model = std::make_shared<rendering_engine::gltf_model>(rendering_engine::load_gltf(path));
            }
            catch (const std::exception& error)
            {
                LOG_WRN("scene assets: could not import %s: %s", file.c_str(), error.what());
                return nullptr;
            }
            keep_alive_while_loading(model);
            return cache.find_mesh(key);
        }

        // -- The material library --------------------------------------------------

        struct material_entry
        {
            std::weak_ptr<rendering_engine::material> instance;
            standard_material_description description;
        };

        struct material_library
        {
            std::unordered_map<std::string, material_entry> by_key;
            std::unordered_map<const rendering_engine::material*, std::string> key_of;
        };

        material_library& library()
        {
            static material_library instance;
            return instance;
        }

        // Drops a dying material's entries; called from its deleter.
        void forget(const rendering_engine::material* doomed)
        {
            material_library& materials = library();
            auto key = materials.key_of.find(doomed);
            if (key == materials.key_of.end())
            {
                return;
            }
            auto entry = materials.by_key.find(key->second);
            if (entry != materials.by_key.end() && entry->second.instance.expired())
            {
                materials.by_key.erase(entry);
            }
            materials.key_of.erase(key);
        }

        // The identity two descriptions share exactly when they describe the
        // same material.
        std::string description_key(const standard_material_description& description)
        {
            using rendering_engine::cache_key_number;
            std::string key = "standard";
            auto number = [&key](auto value)
            {
                key += '|';
                key += cache_key_number(value);
            };
            auto text = [&key](const std::string& value)
            {
                key += '|';
                key += std::to_string(value.size());
                key += ':';
                key += value;
            };
            auto color = [&number](const core::math::vec4& value)
            {
                number(value.x);
                number(value.y);
                number(value.z);
                number(value.w);
            };

            const rendering_engine::material_params& params = description.params;
            number(params.transparent);
            number(params.opacity);
            number(params.double_sided);
            number(static_cast<int>(params.blending));
            number(params.wireframe);
            number(params.depth_test);
            number(params.depth_write);
            number(params.fog);
            color(description.base_color);
            number(description.metalness);
            number(description.roughness);
            color(description.emissive);
            number(description.emissive_intensity);
            number(description.occlusion_strength);
            number(description.ibl_intensity);
            number(description.tangents);
            number(description.skinned);
            text(description.albedo_map);
            text(description.normal_map);
            text(description.metalness_map);
            text(description.roughness_map);
            text(description.occlusion_map);
            text(description.orm_map);
            text(description.emissive_map);
            return key;
        }

        uint8_t to_channel(float value)
        {
            if (!std::isfinite(value))
            {
                return 0;
            }
            return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        rendering_engine::util::color to_color(const core::math::vec4& value)
        {
            return rendering_engine::util::color{
                to_channel(value.x), to_channel(value.y), to_channel(value.z), to_channel(value.w)};
        }

        // Decodes the map at @p path and hands it to @p bind; a map that does
        // not load is left unbound (the description keeps its path).
        template<typename Bind>
        void bind_map(const std::string& path, const char* slot, Bind bind)
        {
            if (path.empty())
            {
                return;
            }
            try
            {
                const rendering_engine::util::image image{path};
                bind(image);
            }
            catch (const std::exception& error)
            {
                LOG_WRN("scene assets: the %s map '%s' could not be loaded (%s); left unbound",
                        slot,
                        path.c_str(),
                        error.what());
            }
        }

        std::unique_ptr<rendering_engine::standard_material> build(const standard_material_description& description)
        {
            using rendering_engine::util::image;
            std::unique_ptr<rendering_engine::standard_material> made =
                current_engine().renderer->create_standard_material();
            rendering_engine::standard_material& target = *made;
            target.set_params(description.params);
            target.set_base_color(to_color(description.base_color));
            target.set_metalness(description.metalness);
            target.set_roughness(description.roughness);
            target.set_emissive(to_color(description.emissive));
            target.set_emissive_intensity(description.emissive_intensity);
            target.set_occlusion_strength(description.occlusion_strength);
            target.set_ibl_intensity(description.ibl_intensity);
            target.set_tangents(description.tangents);
            target.set_skinned(description.skinned);
            bind_map(description.albedo_map, "albedo", [&target](const image& map) { target.set_albedo_map(map); });
            bind_map(description.normal_map, "normal", [&target](const image& map) { target.set_normal_map(map); });
            bind_map(
                description.metalness_map, "metalness", [&target](const image& map) { target.set_metalness_map(map); });
            bind_map(
                description.roughness_map, "roughness", [&target](const image& map) { target.set_roughness_map(map); });
            bind_map(
                description.occlusion_map, "occlusion", [&target](const image& map) { target.set_occlusion_map(map); });
            bind_map(description.orm_map, "orm", [&target](const image& map) { target.set_orm_map(map); });
            bind_map(
                description.emissive_map, "emissive", [&target](const image& map) { target.set_emissive_map(map); });
            return made;
        }
    } // namespace

    std::string file_reference(const std::string& canonical_key)
    {
        const std::optional<std::string> relative =
            core::default_vfs().virtual_path(core::platform::utf8_path(canonical_key));
        return relative.has_value() ? *relative : canonical_key;
    }

    std::string mesh_reference(const rendering_engine::mesh_asset& mesh)
    {
        std::string file;
        std::string fragment;
        if (!split_gltf(mesh.key, file, fragment))
        {
            return mesh.key;
        }
        return std::string{k_gltf_prefix} + file_reference(file) + "#" + fragment;
    }

    std::shared_ptr<rendering_engine::mesh_asset> resolve_mesh_reference(const std::string& reference)
    {
        if (reference.empty())
        {
            return nullptr;
        }

        std::string file;
        std::string fragment;
        if (split_gltf(reference, file, fragment))
        {
            std::shared_ptr<rendering_engine::mesh_asset> mesh = resolve_gltf(file, fragment);
            if (mesh == nullptr)
            {
                LOG_WRN("scene assets: mesh '%s' could not be found in its model", reference.c_str());
            }
            return mesh;
        }

        if (auto live = current_engine().assets->find_mesh(reference))
        {
            return live;
        }
        const mesh_resolver* best = nullptr;
        std::size_t best_length = 0;
        for (const auto& [prefix, resolver] : mesh_resolvers())
        {
            if (reference.starts_with(prefix) && prefix.size() >= best_length)
            {
                best = &resolver;
                best_length = prefix.size();
            }
        }
        if (best != nullptr && *best)
        {
            if (auto rebuilt = (*best)(reference))
            {
                return rebuilt;
            }
        }
        LOG_WRN("scene assets: mesh '%s' is not in the asset cache and no resolver can rebuild it", reference.c_str());
        return nullptr;
    }

    void register_mesh_resolver(std::string prefix, mesh_resolver resolver)
    {
        mesh_resolvers()[std::move(prefix)] = std::move(resolver);
    }

    std::shared_ptr<rendering_engine::material>
    acquire_standard_material(const standard_material_description& description)
    {
        material_library& materials = library();
        const std::string key = description_key(description);
        if (auto found = materials.by_key.find(key); found != materials.by_key.end())
        {
            if (auto live = found->second.instance.lock())
            {
                return live;
            }
        }

        rendering_engine::material* raw = build(description).release();
        // The deleter forgets the description before the material goes, so
        // a later material at the same address is never mistaken for it.
        std::shared_ptr<rendering_engine::material> shared{raw,
                                                           [](rendering_engine::material* doomed)
                                                           {
                                                               forget(doomed);
                                                               delete doomed;
                                                           }};
        materials.by_key[key] = material_entry{shared, description};
        materials.key_of[raw] = key;
        return shared;
    }

    const standard_material_description* find_material_description(const rendering_engine::material& material)
    {
        material_library& materials = library();
        auto key = materials.key_of.find(&material);
        if (key == materials.key_of.end())
        {
            return nullptr;
        }
        auto entry = materials.by_key.find(key->second);
        return entry != materials.by_key.end() ? &entry->second.description : nullptr;
    }
} // namespace runtime
