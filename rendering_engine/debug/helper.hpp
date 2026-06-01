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
#include <rendering_engine/renderables/line.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine::debug
{
    // Base for the debug gizmo family — the analogue of Three.js's
    // @c *Helper objects (grid, axes, bounding box, light, camera
    // frustum). A helper owns a single @ref line renderable drawn as
    // independent segments through the shared @ref line_material, and on
    // construction registers itself twice: into the engine's
    // debug-renderable registry (so the debug pass draws it — debug
    // builds only) and into the process-wide helper registry the debug UI
    // walks to toggle visibility. Simply keeping a helper alive is enough
    // for it to draw; destroying it unregisters from both.
    //
    // Like the rest of the debug-overlay machinery the type is always
    // compiled, but its draws are inert in release: the debug pass is
    // dropped from the pass list there, so a registered helper is never
    // collected.
    struct helper : public renderable
    {
        explicit helper(const char* name);
        ~helper() override;

        helper(const helper&) = delete;
        helper& operator=(const helper&) = delete;

        // Human-readable label shown in the debug UI helper panel.
        const char* name() const noexcept;

        // When false the helper contributes no draws this frame. The
        // debug UI flips this per helper; defaults to visible.
        bool visible{true};

        // World placement of the gizmo. The grid and axes helpers leave
        // this at identity and bake their geometry around the origin; the
        // dynamic helpers (box, light, camera) bake world-space geometry
        // directly and ignore it.
        util::transform transform;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

    protected:
        // Replace the gizmo geometry with @p positions interpreted as
        // independent line segments (vertex pairs) and upload it. The two
        // vectors must be the same length; concrete helpers call this when
        // their geometry changes.
        void set_segments(const std::vector<infrastructure::math::vec3>& positions,
                          const std::vector<infrastructure::math::vec3>& colors);

        // Rebuild geometry from the helper's target before drawing.
        // Static helpers (grid, axes) build once in their constructor and
        // leave this a no-op; helpers that follow a moving target (light,
        // camera) override it to refresh when the target changes. Invoked
        // from @ref collect_draw_items.
        virtual void refresh();

        // Linear-RGB triple in [0, 1] from a 0..255 @ref util::color,
        // ignoring alpha. The debug pass writes straight to the LDR
        // backbuffer (after tonemapping), so the value lands on screen
        // unmodified.
        static infrastructure::math::vec3 to_rgb(const util::color& c);

    private:
        const char* m_name;
        line m_line;
    };

    // The helpers alive right now, in construction order. Owned by the
    // helpers themselves (the vector holds non-owning back-pointers); the
    // debug UI walks it to list every gizmo and toggle its visibility.
    const std::vector<helper*>& registered_helpers();
} // namespace rendering_engine::debug
