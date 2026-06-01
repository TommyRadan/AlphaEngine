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

#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine::debug
{
    // Which pass a helper draws in.
    enum class helper_layer
    {
        // The depth-less debug-overlay pass: always-on-top gizmos that
        // never get occluded (axes, light/camera/box wireframes). Drawn
        // only in debug builds.
        overlay,

        // The 3D scene pass: depth-tested geometry that integrates with
        // the scene and is occluded by it (the infinite ground grid).
        scene,
    };

    // Base for the debug gizmo family — the analogue of Three.js's
    // @c *Helper objects. A helper carries a display name and a
    // visibility flag, and on construction registers itself twice: into
    // the process-wide helper registry the debug UI walks to toggle
    // visibility, and into one of the engine's renderable registries
    // (chosen by @ref helper_layer) so the matching pass draws it.
    // Destroying it unregisters from both.
    //
    // The geometry itself lives in the subclasses: @ref line_helper for
    // the line-based overlay gizmos, @ref infinite_grid for the
    // shader-driven ground grid. Like the rest of the debug-overlay
    // machinery the types are always compiled, but the overlay layer is
    // inert in release where the debug pass is dropped.
    struct helper : public renderable
    {
        helper(const char* name, helper_layer layer);
        ~helper() override;

        helper(const helper&) = delete;
        helper& operator=(const helper&) = delete;

        // Human-readable label shown in the debug UI helper panel.
        const char* name() const noexcept;

        // When false the helper contributes no draws this frame. The
        // debug UI flips this per helper; defaults to visible.
        bool visible{true};

    protected:
        // Linear-RGB triple in [0, 1] from a 0..255 @ref util::color,
        // ignoring alpha.
        static infrastructure::math::vec3 to_rgb(const util::color& c);

    private:
        const char* m_name;
        helper_layer m_layer;
    };

    // The helpers alive right now, in construction order. Owned by the
    // helpers themselves (the vector holds non-owning back-pointers); the
    // debug UI walks it to list every gizmo and toggle its visibility.
    const std::vector<helper*>& registered_helpers();
} // namespace rendering_engine::debug
