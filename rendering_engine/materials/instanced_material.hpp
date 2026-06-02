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

namespace rendering_engine
{
    // Built-in unlit material for @ref instanced_mesh. Position+UV vertex
    // stream like @ref basic_material, but the model matrix and per-instance
    // tint come from a per-draw storage buffer indexed by @c gl_InstanceIndex
    // rather than a single per-draw model UBO — so one draw paints every
    // instance with its own transform and colour.
    //
    // Slot layout: the per-frame group at slot 0 (camera, owned by the
    // @ref scene_pass), the per-draw group at slot 1 (the per-instance
    // storage buffer, built by @ref instanced_mesh), and the per-material
    // group at slot 2 (a flat tint multiplied onto every instance, owned
    // by this material).
    //
    // The per-draw bind group must carry a @c storage_buffer of
    // @ref instanced_mesh records at @ref instance_buffer_binding; only
    // @ref instanced_mesh produces a matching group, so this material is
    // meant to be fronted by it.
    struct instanced_material : public material
    {
        // @p frame_layout is the per-frame bind-group layout owned by the
        // @ref scene_pass; it must match the layout the pass binds at slot
        // 0 every frame so the pipeline and the runtime bind group agree.
        explicit instanced_material(gpu::bind_group_layout frame_layout);
        ~instanced_material() override;

        // Per-draw storage-buffer binding the instance records live at.
        // Exposed so @ref instanced_mesh builds its bind group against the
        // same number this material's pipeline expects.
        static constexpr uint32_t instance_buffer_binding = 1;

        // Flat tint multiplied onto every instance's own colour (white
        // leaves the per-instance colours unchanged).
        void set_color(const util::color& color);

        // The per-material group trails the per-frame and per-draw groups,
        // so it occupies slot 2 (per-frame at 0, per-draw at 1).
        uint32_t per_material_slot() const override;

    private:
        // Push the tint into the per-material UBO.
        void upload_params();

        util::color m_color{255, 255, 255, 255};
        gpu::buffer m_material_ubo{};
    };
} // namespace rendering_engine
