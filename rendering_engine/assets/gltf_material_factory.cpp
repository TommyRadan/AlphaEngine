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

#include <rendering_engine/assets/gltf_material_factory.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/color.hpp>
#include <runtime/engine.hpp>

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

        util::color to_color(const core::math::vec4& factor)
        {
            return util::color{quantize(factor.x), quantize(factor.y), quantize(factor.z), quantize(factor.w)};
        }

        util::color to_color(const core::math::vec3& factor)
        {
            return util::color{quantize(factor.x), quantize(factor.y), quantize(factor.z), 255};
        }
    } // namespace

    std::shared_ptr<standard_material>
    gltf_standard_material_factory::create(const gltf_material_description& description)
    {
        // Adopting the unique_ptr here binds the material's deleter into the
        // shared control block in this renderer-side translation unit; see
        // gltf_material_factory::create for why that matters.
        std::shared_ptr<standard_material> material = runtime::current_engine().renderer->create_standard_material();
        material->set_skinned(description.skinned);
        material->set_base_color(to_color(description.base_color_factor));
        material->set_metalness(std::clamp(description.metallic_factor, 0.0f, 1.0f));
        material->set_roughness(std::clamp(description.roughness_factor, 0.0f, 1.0f));
        material->set_emissive(to_color(description.emissive_factor));
        material->set_emissive_intensity(std::max(description.emissive_strength, 0.0f));

        // Each map binds the cache's texture when the importer supplies one
        // (shared with model.textures, followed by a debug hot reload) and
        // falls back to uploading the decoded image otherwise.
        if (description.base_color_texture != nullptr)
        {
            material->set_albedo_map(description.base_color_texture);
        }
        else if (description.base_color_map != nullptr)
        {
            material->set_albedo_map(*description.base_color_map, description.base_color_space);
        }
        if (description.normal_texture != nullptr)
        {
            material->set_normal_map(description.normal_texture);
        }
        else if (description.normal_map != nullptr)
        {
            material->set_normal_map(*description.normal_map, gpu::color_space::linear);
        }
        if (description.emissive_texture != nullptr)
        {
            material->set_emissive_map(description.emissive_texture);
        }
        else if (description.emissive_map != nullptr)
        {
            material->set_emissive_map(*description.emissive_map, gpu::color_space::srgb);
        }

        // glTF's packed R occlusion / G roughness / B metallic layout is the
        // material's ORM convention, so the decoded image binds as-is. A
        // separate occlusion texture binds beside it and overrides the
        // packed R; one that is the very same image needs no second upload,
        // the packed R already is the occlusion source.
        const bool has_packed = description.metallic_roughness_map != nullptr;
        const bool has_occlusion = description.occlusion_map != nullptr;
        if (description.metallic_roughness_texture != nullptr)
        {
            material->set_orm_map(description.metallic_roughness_texture);
        }
        else if (has_packed)
        {
            material->set_orm_map(*description.metallic_roughness_map, gpu::color_space::linear);
        }
        if (has_occlusion && description.occlusion_map != description.metallic_roughness_map)
        {
            if (description.occlusion_texture != nullptr)
            {
                material->set_occlusion_map(description.occlusion_texture);
            }
            else
            {
                material->set_occlusion_map(*description.occlusion_map, gpu::color_space::linear);
            }
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

    gltf_model load_gltf(const std::filesystem::path& path, const gltf_import_options& options)
    {
        gltf_standard_material_factory factory;
        return load_gltf(path, *runtime::current_engine().assets, factory, options);
    }

    std::shared_ptr<gltf_asset> load_gltf_async(const std::filesystem::path& path, const gltf_import_options& options)
    {
        return runtime::current_engine().assets->load_gltf_async(path, options);
    }
} // namespace rendering_engine
