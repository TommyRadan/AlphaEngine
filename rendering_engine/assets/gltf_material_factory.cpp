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
        material->set_base_color(to_color(description.base_color_factor));
        material->set_metalness(std::clamp(description.metallic_factor, 0.0f, 1.0f));
        material->set_roughness(std::clamp(description.roughness_factor, 0.0f, 1.0f));
        material->set_emissive(to_color(description.emissive_factor));
        material->set_emissive_intensity(std::max(description.emissive_strength, 0.0f));

        if (description.base_color_map != nullptr)
        {
            material->set_albedo_map(*description.base_color_map, description.base_color_space);
        }
        if (description.normal_map != nullptr)
        {
            material->set_normal_map(*description.normal_map, gpu::color_space::linear);
        }
        if (description.metallic_map != nullptr)
        {
            material->set_metalness_map(*description.metallic_map, gpu::color_space::linear);
        }
        if (description.roughness_map != nullptr)
        {
            material->set_roughness_map(*description.roughness_map, gpu::color_space::linear);
        }
        if (description.emissive_map != nullptr)
        {
            material->set_emissive_map(*description.emissive_map, gpu::color_space::srgb);
        }
        return material;
    }

    gltf_model load_gltf(const std::filesystem::path& path, const gltf_import_options& options)
    {
        gltf_standard_material_factory factory;
        return load_gltf(path, *runtime::current_engine().assets, factory, options);
    }
} // namespace rendering_engine
