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

namespace rendering_engine
{
    // Per-frame scene / draw statistics, refreshed by the @ref scene_pass
    // each frame and surfaced read-only through
    // @ref context::get_render_stats (consumed by the debug overlay).
    //
    // The scene pass frustum-culls every renderable that reports
    // @ref renderable::world_bounds against the camera before collecting
    // its draw items, so @ref submitted + @ref culled equals
    // @ref scene_renderables and @ref draw_calls counts only what the
    // survivors emitted. The triangle / vertex counts are the geometry
    // actually submitted to the pipeline this frame, multiplied through
    // instancing. The shadow-pass counters are copied from the shadow
    // passes that ran ahead of the scene pass in the same frame.
    struct render_stats
    {
        // Renderables registered with the scene-renderable registry.
        uint32_t scene_renderables{0};

        // Renderables asked for draw items this frame: those whose world
        // bounds touch the camera frustum plus those with no bounds (always
        // drawn).
        uint32_t submitted{0};

        // Renderables skipped this frame because their world bounds fell
        // entirely outside the camera frustum.
        uint32_t culled{0};

        // Shadow casters skipped by the directional shadow pass because
        // their bounds fell outside the fitted light frustum.
        uint32_t shadow_culled{0};

        // Caster / face pairs skipped by the point shadow pass across its
        // six faces (a caster outside every face counts six times).
        uint32_t point_shadow_culled{0};

        // Draw items submitted this frame (one GPU draw call each). A
        // single renderable may emit more than one.
        uint32_t draw_calls{0};

        // Total instances drawn across every draw item (a non-instanced
        // draw counts as one).
        uint32_t instances{0};

        // Triangles submitted this frame, counting instancing.
        uint64_t triangles{0};

        // Vertices submitted to the vertex stage this frame, counting
        // instancing. For indexed draws this is the index count (vertices
        // fetched), not the unique vertex-buffer size.
        uint64_t vertices{0};
    };
} // namespace rendering_engine
