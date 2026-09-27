// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file hierarchy_panel.cpp
 * @brief The Hierarchy panel: every loaded scene's node tree, with
 *        click-to-select for the Inspector.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <cstddef>

#include <imgui.h>

#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>
#include <runtime/scene_manager.hpp>

namespace editor
{
    namespace
    {
        // True once @p target is found under @p node's subtree; used by
        // validate_selection to drop a selection whose node has since
        // been destroyed, without the Hierarchy panel needing to be open.
        bool node_exists(runtime::node& node, const runtime::node* target)
        {
            if (&node == target)
            {
                return true;
            }
            for (runtime::node* child : node.children())
            {
                if (node_exists(*child, target))
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    // Clears m_selected_node once its node is no longer reachable
    // from any loaded scene's root, so the Inspector never reads a
    // freed node. Run once per frame regardless of which (if any)
    // debug panel is visible.
    void editor_layer::validate_selection()
    {
        if (m_selected_node == nullptr)
        {
            return;
        }
        auto& scenes = *m_engine->scenes;
        for (std::size_t i = 0; i < scenes.scene_count(); ++i)
        {
            if (node_exists(scenes.scene_at(i).root, m_selected_node))
            {
                return;
            }
        }
        m_selected_node = nullptr;
    }

    // One row of the tree, recursing into children when expanded.
    void editor_layer::draw_node_row(runtime::node& node)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool is_leaf = node.children().empty();
        if (is_leaf)
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (&node == m_selected_node)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        const char* name = node.name().c_str();
        const bool inactive = !node.is_active();
        if (inactive)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        // Disambiguates same-named siblings: TreeNodeEx otherwise hashes
        // its id from the label text alone, which two nodes sharing a
        // name would collide on. Popped after TreePop (not right after
        // TreeNodeEx) so the two stay properly nested: TreeNodeEx pushes
        // its own id on top of this one while open, and the children
        // recurse under both.
        ImGui::PushID(&node);
        const bool open = ImGui::TreeNodeEx(name[0] != '\0' ? name : "(unnamed)", flags);
        if (inactive)
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemClicked())
        {
            m_selected_node = &node;
        }

        if (open && !is_leaf)
        {
            for (runtime::node* child : node.children())
            {
                draw_node_row(*child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    // Tree over each loaded scene's root (the persistent scene
    // included), with click-to-select feeding the Inspector panel.
    void editor_layer::draw_hierarchy_window()
    {
        if (!m_show.hierarchy)
        {
            return;
        }

        auto& scenes = *m_engine->scenes;

        ImGui::SetNextWindowSize(ImVec2{280.0f, 380.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Hierarchy", &m_show.hierarchy))
        {
            for (std::size_t i = 0; i < scenes.scene_count(); ++i)
            {
                runtime::scene& scene = scenes.scene_at(i);
                const char* name = scenes.name_at(i).c_str();
                ImGui::PushID(static_cast<int>(i));
                const bool open = ImGui::TreeNodeEx(name[0] != '\0' ? name : "scene",
                                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (open)
                {
                    for (runtime::node* child : scene.root.children())
                    {
                        draw_node_row(*child);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
