# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

# The debug editor. Every translation unit compiles to nothing unless
# ALPHAENGINE_HAS_IMGUI is defined (Debug configurations; see the root
# CMakeLists.txt, which links ImGui and ImGuizmo for Debug only).
target_sources(alpha_editor PRIVATE
    console_panel.cpp
    editor.cpp
    editor.hpp
    editor_layer.hpp
    fps_overlay.cpp
    gizmo.cpp
    helpers_panel.cpp
    hierarchy_panel.cpp
    inspector_panel.cpp
    post_panel.cpp
    profiler_panel.cpp
    render_targets_panel.cpp
    scene_panel.cpp
    settings_panel.cpp
)
