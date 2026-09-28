// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gizmo.cpp
 * @brief The transform gizmo: its controls in the Inspector, the W/E/R
 *        shortcuts, and the ImGuizmo manipulator over the viewport that
 *        writes the selected node's transform.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>

namespace editor
{
    namespace
    {
        // Splits a world (or local) matrix back into position, orientation
        // and scale, so a gizmo edit — which only ever produces a matrix —
        // can be written back onto a core::transform. Mirrors
        // runtime::physics::physics_world.cpp's own decompose(): a mirrored
        // (negative-determinant) result folds its reflection into the X
        // scale so the remaining basis is a proper rotation
        // core::math::quat_from_basis can convert; a collapsed axis has no
        // orientation to recover and is left at the identity rotation.
        core::math::trs decompose_matrix(const core::math::mat4& m)
        {
            core::math::trs pose;
            pose.translation = core::math::vec3{m.m[12], m.m[13], m.m[14]};

            const core::math::vec3 x_axis{m.m[0], m.m[1], m.m[2]};
            const core::math::vec3 y_axis{m.m[4], m.m[5], m.m[6]};
            const core::math::vec3 z_axis{m.m[8], m.m[9], m.m[10]};
            core::math::vec3 scale{core::math::length(x_axis), core::math::length(y_axis), core::math::length(z_axis)};

            constexpr float degenerate = 1.0e-8f;
            if (scale.x < degenerate || scale.y < degenerate || scale.z < degenerate)
            {
                pose.scale = scale;
                return pose;
            }

            core::math::vec3 x_unit = x_axis / scale.x;
            const core::math::vec3 y_unit = y_axis / scale.y;
            const core::math::vec3 z_unit = z_axis / scale.z;
            if (core::math::dot(core::math::cross(x_unit, y_unit), z_unit) < 0.0f)
            {
                scale.x = -scale.x;
                x_unit = -x_unit;
            }
            pose.scale = scale;
            pose.rotation = core::math::normalize(core::math::quat_from_basis(x_unit, y_unit, z_unit));
            return pose;
        }

        // True while no ImGui panel holds keyboard/mouse focus, i.e. focus
        // is on the passthrough central dock node (the game view) rather
        // than a docked tool window. Gates the W/E/R shortcuts below so
        // typing "w" into, say, the Inspector's Name field or the
        // Console's category filter does not retarget the gizmo.
        bool viewport_has_focus()
        {
            return !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
        }
    } // namespace

    // Mode toggle and optional snapping for the transform gizmo
    // drawn over the viewport by draw_gizmo. The W/E/R shortcuts
    // (handle_gizmo_shortcuts) change the same m_gizmo.operation, so
    // the radio buttons here always reflect whichever one fired last.
    void editor_layer::draw_gizmo_controls()
    {
        ImGui::SeparatorText("Gizmo");
        if (ImGui::RadioButton("Translate (W)", m_gizmo.operation == ImGuizmo::TRANSLATE))
        {
            m_gizmo.operation = ImGuizmo::TRANSLATE;
        }
        if (ImGui::RadioButton("Rotate (E)", m_gizmo.operation == ImGuizmo::ROTATE))
        {
            m_gizmo.operation = ImGuizmo::ROTATE;
        }
        if (ImGui::RadioButton("Scale (R)", m_gizmo.operation == ImGuizmo::SCALE))
        {
            m_gizmo.operation = ImGuizmo::SCALE;
        }

        // ImGuizmo always manipulates SCALE in local space regardless of
        // this setting, so the toggle is hidden while it would do nothing.
        if (m_gizmo.operation != ImGuizmo::SCALE)
        {
            if (ImGui::RadioButton("Local##gizmo_space", m_gizmo.mode == ImGuizmo::LOCAL))
            {
                m_gizmo.mode = ImGuizmo::LOCAL;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("World##gizmo_space", m_gizmo.mode == ImGuizmo::WORLD))
            {
                m_gizmo.mode = ImGuizmo::WORLD;
            }
        }

        ImGui::Checkbox("Snap##gizmo", &m_gizmo.snap_enabled);
        switch (m_gizmo.operation)
        {
        case ImGuizmo::ROTATE:
            ImGui::DragFloat("Snap degrees##gizmo", &m_gizmo.snap_rotate_degrees, 1.0f, 0.0f, 180.0f);
            break;
        case ImGuizmo::SCALE:
            ImGui::DragFloat("Snap scale##gizmo", &m_gizmo.snap_scale, 0.01f, 0.01f, 10.0f);
            break;
        default:
            ImGui::DragFloat("Snap step##gizmo", &m_gizmo.snap_translate, 0.05f, 0.01f, 100.0f);
            break;
        }
    }

    // W/E/R cycle the gizmo between translate / rotate / scale, matching
    // the Inspector's radio buttons (draw_gizmo_controls). Checked every
    // frame regardless of whether a node is selected, same as the
    // Inspector toggle.
    void editor_layer::handle_gizmo_shortcuts()
    {
        if (!viewport_has_focus())
        {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W))
        {
            m_gizmo.operation = ImGuizmo::TRANSLATE;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E))
        {
            m_gizmo.operation = ImGuizmo::ROTATE;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R))
        {
            m_gizmo.operation = ImGuizmo::SCALE;
        }
    }

    // Translate / rotate / scale gizmo on the Hierarchy's current
    // selection, drawn over @p dockspace_id's central node (the
    // passthrough game view) in the active camera's view and
    // projection. ImGuizmo::Manipulate always works in world space —
    // m_gizmo.mode only orients the translate/rotate handles, and it
    // ignores the mode entirely for scale — so the manipulated result
    // is converted back to the node's local transform by undoing its
    // parent's world matrix (a root node's local and world already
    // agree). A node whose pose is driven by physics or an animator is
    // simply overwritten again next frame, same as any other manual
    // edit to its transform.
    void editor_layer::draw_gizmo(ImGuiID dockspace_id)
    {
        if (m_selected_node == nullptr)
        {
            return;
        }
        const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace_id);
        rendering_engine::camera* cam = m_engine->renderer->world().active_camera();
        if (central == nullptr || cam == nullptr)
        {
            return;
        }

        ImGuizmo::SetOrthographic(dynamic_cast<rendering_engine::orthographic_camera*>(cam) != nullptr);
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(central->Pos.x, central->Pos.y, central->Size.x, central->Size.y);

        runtime::node& node = *m_selected_node;
        core::math::mat4 world = node.world_matrix();
        const core::math::mat4 view = cam->get_view_matrix();
        const core::math::mat4 projection = cam->get_projection_matrix();

        float snap[3] = {m_gizmo.snap_translate, m_gizmo.snap_translate, m_gizmo.snap_translate};
        if (m_gizmo.operation == ImGuizmo::ROTATE)
        {
            snap[0] = snap[1] = snap[2] = m_gizmo.snap_rotate_degrees;
        }
        else if (m_gizmo.operation == ImGuizmo::SCALE)
        {
            snap[0] = snap[1] = snap[2] = m_gizmo.snap_scale;
        }

        ImGuizmo::Manipulate(view.data(),
                             projection.data(),
                             m_gizmo.operation,
                             m_gizmo.mode,
                             world.data(),
                             nullptr,
                             m_gizmo.snap_enabled ? snap : nullptr);

        if (!ImGuizmo::IsUsing())
        {
            return;
        }

        runtime::node* parent = node.parent();
        const core::math::mat4 local = parent != nullptr ? core::math::inverse(parent->world_matrix()) * world : world;
        const core::math::trs pose = decompose_matrix(local);
        node.transform.set_position(pose.translation);
        node.transform.set_quaternion(pose.rotation);
        node.transform.set_scale(pose.scale);
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
