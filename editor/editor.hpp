// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file editor.hpp
 * @brief The debug editor: a Dear ImGui + ImGuizmo overlay over the
 *        running engine (hierarchy, inspector and transform gizmo,
 *        console, render-target viewer, profiler, scene statistics,
 *        settings, post-processing and debug-helper panels).
 *
 * The editor sits above @c runtime, @c rendering_engine and
 * @c platform: it reads and edits the world through the engine it is
 * handed, draws through the GPU device's @c gpu::overlay_renderer, and
 * sees the window's events through the event filter it installs
 * (@c platform::window::set_event_filter). Nothing below it includes
 * this module; the executable creates the editor and installs it on the
 * engine as its @ref runtime::overlay.
 *
 * Debug builds only: ImGui and ImGuizmo are linked, and
 * @c ALPHAENGINE_HAS_IMGUI defined, for Debug configurations alone (see
 * the root CMakeLists.txt). Every translation unit of the module
 * compiles to nothing without that macro, so a caller includes this
 * header and calls @ref create_overlay under the same guard.
 */

#pragma once

#include <memory>

#include <runtime/overlay.hpp>

namespace editor
{
    /**
     * @brief A new editor, not yet brought up, as the overlay the engine
     *        drives (@c runtime::engine::set_overlay). The engine owns it
     *        from then on; all panel state lives in it.
     */
    std::unique_ptr<runtime::overlay> create_overlay();
} // namespace editor
