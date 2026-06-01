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
    // Built-in point-cloud scene material — the analogue of
    // @c THREE.PointsMaterial. Renders a point-list vertex stream as
    // screen-space squares whose pixel size comes from a per-material
    // point size; each point is filled with a flat colour optionally
    // modulated by a sprite texture sampled with @c gl_PointCoord.
    //
    // Slot layout matches the other 3D scene materials: the per-frame
    // group at slot 0 (camera, owned by the @ref scene_pass) and the
    // per-draw group at slot 1 (@c modelMatrix, built by each
    // renderable). The colour, point size and optional sprite live in
    // the per-material group at slot 2 owned by this material.
    struct points_material : public material
    {
        // @p frame_layout is the per-frame bind-group layout owned by
        // the @ref scene_pass; it must match the layout the pass binds
        // at slot 0 every frame so the pipeline and the runtime bind
        // group agree on slot shape.
        explicit points_material(gpu::bind_group_layout frame_layout);
        ~points_material() override;

        // Point colour tint. When a sprite is set the sampled texel is
        // multiplied by this tint (white leaves it unchanged).
        void set_color(const util::color& color);

        // Screen-space point size in pixels written to @c gl_PointSize.
        void set_point_size(float size);

        // Bind a sprite texture; the fragment shader switches to
        // sampling it (modulated by the tint) using @c gl_PointCoord.
        // Replaces any previous sprite and rebuilds the per-material
        // bind group.
        void set_sprite(const util::image& image);

        // Drop the sprite texture; the material falls back to the flat
        // tint. No-op when no sprite is set.
        void clear_sprite();

        // The per-material group trails the per-frame and per-draw
        // groups, so it occupies slot 2 (per-frame at 0, per-draw at 1).
        uint32_t per_material_slot() const override;

    private:
        // (Re)create the per-material bind group against the current
        // sprite texture handle, then push the latest tint / size /
        // sprite flag into the UBO.
        void rebuild_bind_group();

        // Push {color, pointSize, useTexture} into the per-material UBO.
        void upload_params();

        util::color m_color{255, 255, 255, 255};
        float m_point_size{1.0f};
        gpu::buffer m_material_ubo{};
        gpu::texture m_sprite{};
    };
} // namespace rendering_engine
