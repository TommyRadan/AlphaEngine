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
     *        @ref context::create_standard_material and applies the
     *        description's factors and maps.
     *
     * Factors are quantised to the material's 8-bit colours; the base
     * colour map uploads in the description's colour space, the emissive
     * map as sRGB and the normal / metallic / roughness maps as linear.
     * Needs a live renderer (it reaches @ref runtime::current_engine), which
     * is why it lives in its own translation unit apart from the importer.
     */
    struct gltf_standard_material_factory final : gltf_material_factory
    {
        std::shared_ptr<standard_material> create(const gltf_material_description& description) override;
    };
} // namespace rendering_engine
