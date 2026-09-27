// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file inspector_panel.cpp
 * @brief The Inspector panel: the selected node's name, active flag and
 *        transform, the gizmo controls, and a section per built-in
 *        component it carries.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <array>
#include <cstdint>
#include <cstdio>

#include <imgui.h>

#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/lighting/spot_light.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/renderables/model.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/node.hpp>

namespace editor
{
    namespace
    {
        // Degree <-> radian conversion for the transform / camera / spot
        // light angle fields, which the engine stores in radians but are
        // far more legible to edit in degrees.
        constexpr float k_rad_to_deg = 57.295779513082320876798154814105f;
        constexpr float k_deg_to_rad = 0.017453292519943295769236907684886f;

        void draw_transform_section(core::transform& transform)
        {
            core::math::vec3 position = transform.get_position();
            if (ImGui::DragFloat3("Position", position.data(), 0.05f))
            {
                transform.set_position(position);
            }
            core::math::vec3 rotation = transform.get_rotation() * k_rad_to_deg;
            if (ImGui::DragFloat3("Rotation", rotation.data(), 0.5f))
            {
                transform.set_rotation(rotation * k_deg_to_rad);
            }
            core::math::vec3 scale = transform.get_scale();
            if (ImGui::DragFloat3("Scale", scale.data(), 0.02f))
            {
                transform.set_scale(scale);
            }
        }

        void draw_inspector(runtime::camera_component& component)
        {
            rendering_engine::camera* camera = component.get();
            if (camera == nullptr)
            {
                ImGui::TextDisabled("empty");
                return;
            }

            bool enabled = camera->is_enabled();
            if (ImGui::Checkbox("Enabled##camera", &enabled))
            {
                camera->set_enabled(enabled);
            }
            bool main = camera->is_main();
            if (ImGui::Checkbox("Main##camera", &main))
            {
                camera->set_main(main);
            }
            int priority = camera->get_priority();
            if (ImGui::DragInt("Priority##camera", &priority))
            {
                camera->set_priority(priority);
            }

            if (auto* persp = dynamic_cast<rendering_engine::perspective_camera*>(camera))
            {
                float fov_deg = persp->get_field_of_view() * k_rad_to_deg;
                if (ImGui::DragFloat("Field of view", &fov_deg, 0.5f, 1.0f, 179.0f))
                {
                    persp->set_field_of_view(fov_deg * k_deg_to_rad);
                }
                float near_clip = persp->get_near_clip();
                if (ImGui::DragFloat("Near##persp", &near_clip, 0.01f, 0.001f, persp->get_far_clip()))
                {
                    persp->set_near_clip(near_clip);
                }
                float far_clip = persp->get_far_clip();
                if (ImGui::DragFloat("Far##persp", &far_clip, 1.0f, persp->get_near_clip(), 1000000.0f))
                {
                    persp->set_far_clip(far_clip);
                }
            }
            else if (auto* ortho = dynamic_cast<rendering_engine::orthographic_camera*>(camera))
            {
                float x_mag = ortho->get_x_magnification();
                if (ImGui::DragFloat("X magnification", &x_mag, 0.05f))
                {
                    ortho->set_x_magnification(x_mag);
                }
                float y_mag = ortho->get_y_magnification();
                if (ImGui::DragFloat("Y magnification", &y_mag, 0.05f))
                {
                    ortho->set_y_magnification(y_mag);
                }
                float near_clip = ortho->get_near_clip();
                if (ImGui::DragFloat("Near##ortho", &near_clip, 0.01f))
                {
                    ortho->set_near_clip(near_clip);
                }
                float far_clip = ortho->get_far_clip();
                if (ImGui::DragFloat("Far##ortho", &far_clip, 1.0f))
                {
                    ortho->set_far_clip(far_clip);
                }
            }
        }

        void draw_inspector(runtime::light_component& component)
        {
            rendering_engine::light* light = component.get();
            if (light == nullptr)
            {
                ImGui::TextDisabled("empty");
                return;
            }

            bool enabled = light->is_enabled();
            if (ImGui::Checkbox("Enabled##light", &enabled))
            {
                light->set_enabled(enabled);
            }
            ImGui::ColorEdit3("Color##light", light->color.data());
            ImGui::DragFloat("Intensity##light", &light->intensity, 0.05f, 0.0f, 1000.0f);

            switch (light->type())
            {
            case rendering_engine::light_type::ambient:
                ImGui::TextDisabled("ambient — no spatial parameters");
                break;
            case rendering_engine::light_type::directional:
            {
                auto* directional = static_cast<rendering_engine::directional_light*>(light);
                ImGui::DragFloat3("Direction##light", directional->direction.data(), 0.01f);
                ImGui::Checkbox("Cast shadow##light", &directional->cast_shadow);
                break;
            }
            case rendering_engine::light_type::point:
            {
                auto* point = static_cast<rendering_engine::point_light*>(light);
                ImGui::DragFloat3("Position##light", point->position.data(), 0.05f);
                ImGui::DragFloat("Range##light", &point->range, 0.1f, 0.0f, 100000.0f);
                ImGui::DragFloat("Constant atten.##light", &point->constant_attenuation, 0.01f);
                ImGui::DragFloat("Linear atten.##light", &point->linear_attenuation, 0.001f);
                ImGui::DragFloat("Quadratic atten.##light", &point->quadratic_attenuation, 0.001f);
                ImGui::Checkbox("Cast shadow##light", &point->cast_shadow);
                break;
            }
            case rendering_engine::light_type::spot:
            {
                auto* spot = static_cast<rendering_engine::spot_light*>(light);
                ImGui::DragFloat3("Position##light", spot->position.data(), 0.05f);
                ImGui::DragFloat3("Direction##light", spot->direction.data(), 0.01f);
                ImGui::DragFloat("Range##light", &spot->range, 0.1f, 0.0f, 100000.0f);
                ImGui::DragFloat("Constant atten.##light", &spot->constant_attenuation, 0.01f);
                ImGui::DragFloat("Linear atten.##light", &spot->linear_attenuation, 0.001f);
                ImGui::DragFloat("Quadratic atten.##light", &spot->quadratic_attenuation, 0.001f);
                float outer_deg = spot->outer_angle * k_rad_to_deg;
                if (ImGui::DragFloat("Outer angle##light", &outer_deg, 0.5f, 0.0f, 89.0f))
                {
                    spot->outer_angle = outer_deg * k_deg_to_rad;
                }
                float inner_deg = spot->inner_angle * k_rad_to_deg;
                if (ImGui::DragFloat("Inner angle##light", &inner_deg, 0.5f, 0.0f, 89.0f))
                {
                    spot->inner_angle = inner_deg * k_deg_to_rad;
                }
                ImGui::Checkbox("Cast shadow##light", &spot->cast_shadow);
                break;
            }
            }
        }

        void draw_inspector(runtime::mesh_component& component)
        {
            rendering_engine::model* model = component.model();
            if (model == nullptr)
            {
                ImGui::TextDisabled("empty");
                return;
            }

            rendering_engine::material* material = model->get_material();
            if (material == nullptr)
            {
                ImGui::TextDisabled("no material");
                return;
            }

            rendering_engine::material_params params = material->params();
            bool changed = false;
            changed |= ImGui::Checkbox("Transparent##mat", &params.transparent);
            changed |= ImGui::SliderFloat("Opacity##mat", &params.opacity, 0.0f, 1.0f);
            changed |= ImGui::Checkbox("Double-sided##mat", &params.double_sided);
            static constexpr std::array<const char*, 5> blend_names = {
                "None", "Normal", "Additive", "Subtractive", "Multiply"};
            int blend = static_cast<int>(params.blending);
            if (ImGui::Combo("Blending##mat", &blend, blend_names.data(), static_cast<int>(blend_names.size())))
            {
                params.blending = static_cast<rendering_engine::blend_mode>(blend);
                changed = true;
            }
            changed |= ImGui::Checkbox("Wireframe##mat", &params.wireframe);
            changed |= ImGui::Checkbox("Depth test##mat", &params.depth_test);
            changed |= ImGui::Checkbox("Depth write##mat", &params.depth_write);
            changed |= ImGui::Checkbox("Fog##mat", &params.fog);
            if (changed)
            {
                material->set_params(params);
            }

            if (ImGui::TreeNode("Keywords##mat"))
            {
                const uint32_t keywords = material->keywords();
                for (const rendering_engine::material_keyword keyword : rendering_engine::all_material_keywords)
                {
                    bool set = (keywords & rendering_engine::keyword_bit(keyword)) != 0;
                    ImGui::BeginDisabled(true);
                    ImGui::Checkbox(rendering_engine::keyword_define(keyword), &set);
                    ImGui::EndDisabled();
                }
                ImGui::TreePop();
            }
        }
    } // namespace

    // The selected node's name, active flag and transform, plus a
    // hand-written section per built-in component it carries. Kept in
    // the editor rather than on the components so release builds carry
    // none of this.
    void editor_layer::draw_inspector_window()
    {
        if (!m_show.inspector)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2{320.0f, 420.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Inspector", &m_show.inspector))
        {
            if (m_selected_node == nullptr)
            {
                ImGui::TextDisabled("no node selected");
            }
            else
            {
                runtime::node& node = *m_selected_node;

                if (m_inspector.name_node != &node)
                {
                    m_inspector.name_node = &node;
                    std::snprintf(
                        m_inspector.name_buffer.data(), m_inspector.name_buffer.size(), "%s", node.name().c_str());
                }
                ImGui::InputText("Name", m_inspector.name_buffer.data(), m_inspector.name_buffer.size());
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    node.set_name(m_inspector.name_buffer.data());
                }

                bool active = node.is_active();
                if (ImGui::Checkbox("Active", &active))
                {
                    node.set_active(active);
                }

                ImGui::SeparatorText("Transform");
                draw_transform_section(node.transform);
                draw_gizmo_controls();

                if (runtime::camera_component* camera = node.get_component<runtime::camera_component>())
                {
                    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        draw_inspector(*camera);
                    }
                }
                if (runtime::light_component* light = node.get_component<runtime::light_component>())
                {
                    if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        draw_inspector(*light);
                    }
                }
                if (runtime::mesh_component* mesh = node.get_component<runtime::mesh_component>())
                {
                    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        draw_inspector(*mesh);
                    }
                }
            }
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
