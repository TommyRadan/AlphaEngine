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

#include <cstdint>
#include <memory>

#include <core/math/vec2.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine
{
    // One vertex of the stream the ui material reads
    // (shaders/materials/ui.vert.glsl). UI space is drawable pixels with
    // (0, 0) at the top-left corner, +x right and +y down, and nothing is
    // baked into a vertex that depends on the drawable's size: the shader
    // resolves each position against the live pixel size from the ui
    // pass's per-frame block, so anchored and stretched elements follow a
    // resize without being re-uploaded.
    //
    // A quad is its pivot plus four corners around it. The pivot lands at
    // @c pivot_anchor * size + pivot_offset (the anchor a fraction of the
    // drawable, the offset in pixels, snapped to whole pixels); each
    // corner sits @c corner_anchor * size + corner_offset from the pivot,
    // turned about it by @c rotation (cos, sin; positive turns clockwise
    // on screen). @c corner_anchor is non-zero only on the stretched axes
    // of a rect whose anchors differ (see @ref rect_transform). The
    // fragment is @c texture(uv) * color.
    struct ui_vertex
    {
        core::math::vec2 pivot_anchor{0.0f, 0.0f};
        core::math::vec2 pivot_offset{0.0f, 0.0f};
        core::math::vec2 corner_anchor{0.0f, 0.0f};
        core::math::vec2 corner_offset{0.0f, 0.0f};
        core::math::vec2 rotation{1.0f, 0.0f};
        core::math::vec2 uv{0.0f, 0.0f};
        util::color color{255, 255, 255, 255};
    };

    static_assert(sizeof(ui_vertex) == 52, "ui_vertex must be six packed vec2s and one RGBA8 colour");

    // Built-in 2D overlay material. Reads the @ref ui_vertex stream,
    // transforms it with the ui pass's per-frame block (the pixel-space
    // orthographic projection and the drawable size) at slot 0, and
    // writes texture x vertex colour, straight alpha, depth off. The
    // per-draw bind group at slot 1 carries the one sampled texture:
    // an image, a font atlas (white, coverage in alpha, so the vertex
    // colour tints the text) or @ref white_texture for a flat colour.
    struct ui_material : public material
    {
        // Binding of the UiFrame block in the ui pass's per-frame group (set 0).
        static constexpr uint32_t frame_binding = 0;
        // Binding of the sampled texture in the per-draw group (set 1).
        static constexpr uint32_t texture_binding = 1;

        // @p tmpl is the ui template (see @ref create_template). Creates
        // @ref white_texture on the template's device.
        explicit ui_material(std::shared_ptr<material_template> tmpl);
        ~ui_material() override;

        // The per-frame layout the ui pass creates and binds at slot 0:
        // one uniform buffer at @ref frame_binding.
        static gpu::bind_group_layout_descriptor frame_layout_descriptor();

        // The template every ui_material shares, over the ui pass's
        // per-frame layout @p frame_layout (built from
        // @ref frame_layout_descriptor).
        static std::shared_ptr<material_template> create_template(gpu::device& device,
                                                                  gpu::bind_group_layout frame_layout);

        // A 1x1 opaque white texture owned by the material: what an
        // untextured quad samples so texture x colour is the colour.
        gpu::texture white_texture() const;

    private:
        gpu::texture m_white_texture{};
    };
} // namespace rendering_engine
