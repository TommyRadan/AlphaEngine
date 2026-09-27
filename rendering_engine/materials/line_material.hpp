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

#include <memory>

#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    // Built-in unlit line material. Its pipeline bakes
    // @c primitive_topology::lines, so a @ref line renderable that
    // fronts it rasterizes one line segment per index/vertex pair (a
    // connected strip or independent segments depending on the
    // renderable's mode).
    //
    // Vertex stream: position (vec3) + per-vertex colour (vec3). The
    // fragment colour is the per-vertex colour modulated by the shared
    // tint.
    //
    // Slot layout mirrors @ref basic_material: the per-frame group at
    // slot 0 (camera, owned by the @ref scene_pass), the per-draw group
    // at slot 1 (model + normal matrix, built by the renderable), and
    // the tint in the per-material group at slot 2 owned by each
    // instance. The scene line material and the depth-less debug line
    // material are two instances of one template, bound to two
    // pipeline variants.
    //
    // Line width is fixed at one pixel: the Vulkan backend pins
    // @c lineWidth to 1.0 and wide lines are not portable on either
    // backend, so the @c linewidth knob is intentionally omitted.
    struct line_material : public material
    {
        // @p tmpl is the shared line template (see @ref create_template).
        //
        // @p depth_tested keeps the default opaque scene behaviour
        // (depth tested and written). Pass @c false for lines drawn in a
        // depth-less pass such as the debug-overlay pass, where the
        // swapchain depth buffer is not cleared per frame and a
        // depth-writing line would occlude itself on the next frame; the
        // debug gizmos use such an instance so they always draw on top.
        // Either can be changed later through @ref set_depth_test /
        // @ref set_depth_write.
        explicit line_material(std::shared_ptr<material_template> tmpl, bool depth_tested = true);
        ~line_material() override;

        // The template every line_material shares. @p frame_layout is
        // the per-frame bind-group layout owned by the @ref scene_pass;
        // it must match the layout the pass binds at slot 0 every frame
        // so the pipelines and the runtime bind group agree on slot shape.
        static std::shared_ptr<material_template> create_template(gpu::device& device,
                                                                  gpu::bind_group_layout frame_layout);

        // Tint multiplied into every vertex's colour (white leaves the
        // per-vertex colour unchanged). Alpha participates when the
        // material is transparent.
        void set_color(const color& color);

    private:
        // Push the tint colour into the per-material UBO.
        void upload_params();

        color m_color{255, 255, 255, 255};
        gpu::buffer m_material_ubo{};
    };
} // namespace rendering_engine
