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

#pragma once

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/image.hpp>

namespace rendering_engine
{
    // Built-in unlit 3D scene material. Position+UV vertex stream, MVP
    // transform in the vertex shader, fragment colour is a flat base
    // tint optionally modulated by an albedo texture.
    //
    // Slot layout: the per-frame group at slot 0 (camera + lights,
    // owned by the @ref scene_pass) and the per-draw group at slot 1
    // (@c modelMatrix, built by each renderable) match the previous
    // built-in 3D material, so renderables need no changes. The tint
    // and texture live in the per-material group at slot 2 owned by
    // this material — every renderable that fronts it shares them.
    struct basic_material : public material
    {
        // @p frame_layout is the per-frame bind-group layout owned by
        // the @ref scene_pass; it must match the layout the pass binds
        // at slot 0 every frame so the pipeline and the runtime bind
        // group agree on slot shape.
        explicit basic_material(gpu::bind_group_layout frame_layout);
        ~basic_material() override;

        // Base colour tint. When an albedo texture is set the sampled
        // texel is multiplied by this tint (white leaves it unchanged).
        void set_color(const util::color& color);

        // Bind an albedo texture; the fragment shader switches to
        // sampling it (modulated by the tint). Replaces any previous
        // texture and rebuilds the per-material bind group.
        void set_albedo(const util::image& image);

        // Drop the albedo texture; the material falls back to the flat
        // tint. No-op when no texture is set.
        void clear_albedo();

        // The per-material group trails the per-frame and per-draw
        // groups, so it occupies slot 2 (per-frame at 0, per-draw at 1).
        uint32_t per_material_slot() const override;

    private:
        // (Re)create the per-material bind group against the current
        // albedo texture handle, then push the latest tint / texture
        // flag into the UBO.
        void rebuild_bind_group();

        // Push {color, useTexture} into the per-material UBO.
        void upload_params();

        util::color m_color{255, 255, 255, 255};
        gpu::buffer m_material_ubo{};
        gpu::texture m_albedo{};
    };
} // namespace rendering_engine
