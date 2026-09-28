// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file debug_visuals.cpp
 * @brief The editor's built-in debug visualisations — the world axes, the
 *        physics wireframe, light and camera gizmos — drawn each frame
 *        through the debug-draw functions, and the debug-draw text labels.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <imgui.h>

#include <core/math/math.hpp>
#include <rendering_engine/debug_draw/debug_draw.hpp>
#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>
#include <runtime/physics/physics_debug_draw.hpp>
#include <runtime/physics/physics_world.hpp>

namespace editor
{
    namespace
    {
        namespace debug_draw = rendering_engine::debug_draw;
        namespace math = core::math;

        // How large the light gizmos are, in world units.
        constexpr float light_gizmo_size = 1.0f;

        // Points around a spot light's cone base; every spoke_stride-th one
        // also draws a spoke back to the apex, so the gizmo reads as a cone
        // rather than a flat ring.
        constexpr int spot_ring_segments = 16;
        constexpr int spot_spoke_stride = spot_ring_segments / 4;

        // The tint of a camera's frustum.
        constexpr math::vec3 camera_color{200.0f / 255.0f, 200.0f / 255.0f, 80.0f / 255.0f};

        // A light's radiance is unbounded (colour times intensity is
        // applied later), so each channel is clamped into the displayable
        // [0, 1] before it tints the gizmo.
        math::vec3 gizmo_color(const rendering_engine::light_proxy& light)
        {
            return math::vec3{std::clamp(light.color.x, 0.0f, 1.0f),
                              std::clamp(light.color.y, 0.0f, 1.0f),
                              std::clamp(light.color.z, 0.0f, 1.0f)};
        }

        // Right and up axes around @p dir, on the engine up axis;
        // reference_up swaps in a horizontal axis when @p dir points
        // straight up or down, so the cross product stays stable.
        void basis_around(const math::vec3& dir, math::vec3& right, math::vec3& up)
        {
            right = math::normalize(math::cross(math::reference_up(dir), dir));
            up = math::normalize(math::cross(dir, right));
        }

        // A directional light has no position: a small square facing its
        // travel direction at the world origin, and a ray along it.
        void draw_directional_light(rendering_engine::render_world& world, const rendering_engine::light_proxy& light)
        {
            const math::vec3 dir = math::normalize(light.direction);
            math::vec3 right;
            math::vec3 up;
            basis_around(dir, right, up);

            const math::vec3 origin{0.0f, 0.0f, 0.0f};
            const float half = light_gizmo_size * 0.25f;
            const std::array<math::vec3, 4> square{origin + right * half + up * half,
                                                   origin - right * half + up * half,
                                                   origin - right * half - up * half,
                                                   origin + right * half - up * half};
            const math::vec3 color = gizmo_color(light);
            for (std::size_t i = 0; i < square.size(); ++i)
            {
                debug_draw::line(world, square[i], square[(i + 1) % square.size()], color);
            }
            debug_draw::line(world, origin, origin + dir * light_gizmo_size, color);
        }

        // A point light: an octahedron around its position, with its apexes
        // on the engine up axis.
        void draw_point_light(rendering_engine::render_world& world, const rendering_engine::light_proxy& light)
        {
            const math::vec3& position = light.position;
            const float r = light_gizmo_size;
            const math::vec3 top = position + math::world_up * r;
            const math::vec3 bottom = position - math::world_up * r;
            const std::array<math::vec3, 4> ring{position + math::vec3{r, 0.0f, 0.0f},
                                                 position + math::vec3{0.0f, r, 0.0f},
                                                 position - math::vec3{r, 0.0f, 0.0f},
                                                 position - math::vec3{0.0f, r, 0.0f}};
            const math::vec3 color = gizmo_color(light);
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const math::vec3& a = ring[i];
                debug_draw::line(world, a, ring[(i + 1) % ring.size()], color);
                debug_draw::line(world, a, top, color);
                debug_draw::line(world, a, bottom, color);
            }
        }

        // A spot light: a cone from its position along its direction,
        // opening to the outer cone half-angle.
        void draw_spot_light(rendering_engine::render_world& world, const rendering_engine::light_proxy& light)
        {
            const math::vec3 dir = math::normalize(light.direction);
            math::vec3 right;
            math::vec3 up;
            basis_around(dir, right, up);

            const math::vec3& apex = light.position;
            const math::vec3 base_center = apex + dir * light_gizmo_size;
            const float radius = light_gizmo_size * std::tan(light.outer_angle);
            const math::vec3 color = gizmo_color(light);

            std::array<math::vec3, spot_ring_segments> ring{};
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const float angle = math::two_pi * static_cast<float>(i) / static_cast<float>(spot_ring_segments);
                ring[i] = base_center + right * (std::cos(angle) * radius) + up * (std::sin(angle) * radius);
            }
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                debug_draw::line(world, ring[i], ring[(i + 1) % ring.size()], color);
                if (i % spot_spoke_stride == 0)
                {
                    debug_draw::line(world, apex, ring[i], color);
                }
            }
        }
    } // namespace

    // Records what the Helpers panel's toggles ask for into the renderer's
    // world, after the render extraction, so the light and camera gizmos
    // follow the proxies this frame draws with.
    void editor_layer::draw_debug_visuals()
    {
        rendering_engine::render_world& world = m_engine->renderer->world();

        if (m_visuals.axes)
        {
            debug_draw::axes(world, math::mat4{});
        }
        if (m_visuals.physics)
        {
            runtime::physics::draw_debug(*m_engine->physics, world);
        }
        if (m_visuals.lights)
        {
            for (const rendering_engine::light_proxy_handle handle : world.enabled_lights())
            {
                const rendering_engine::light_proxy* light = world.light(handle);
                if (light == nullptr)
                {
                    continue;
                }
                switch (light->type)
                {
                case rendering_engine::light_type::directional:
                    draw_directional_light(world, *light);
                    break;
                case rendering_engine::light_type::point:
                    draw_point_light(world, *light);
                    break;
                case rendering_engine::light_type::spot:
                    draw_spot_light(world, *light);
                    break;
                default:
                    break;
                }
            }
        }
        if (m_visuals.cameras)
        {
            // The rendering camera's own frustum would enclose the whole
            // view, so it is left out.
            const rendering_engine::camera_proxy_handle active = world.active_camera();
            for (std::size_t i = 0; i < world.cameras().size(); ++i)
            {
                const rendering_engine::camera_proxy& camera = world.cameras()[i];
                if (!camera.enabled || world.camera_at(i) == active)
                {
                    continue;
                }
                debug_draw::frustum(world, camera.projection * camera.view, camera_color);
            }
        }
    }

    // Draws the debug-draw list's text labels over the whole frame, each
    // anchored where the active camera sees its world-space position: the
    // rendered frame covers the main viewport, whose clip space is Y-up
    // with depth in [0, 1]. A label behind the camera or past its far plane
    // is skipped.
    void editor_layer::draw_debug_text()
    {
        const rendering_engine::render_world& world = m_engine->renderer->world();
        const auto labels = world.debug_draw_list().texts();
        const rendering_engine::camera_proxy* camera = world.camera(world.active_camera());
        if (labels.empty() || camera == nullptr)
        {
            return;
        }

        const math::mat4 view_projection = camera->projection * camera->view;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        for (const rendering_engine::debug_draw::text_label& label : labels)
        {
            const math::vec4 clip =
                view_projection * math::vec4{label.position.x, label.position.y, label.position.z, 1.0f};
            if (clip.w <= 0.0f || clip.z > clip.w)
            {
                continue;
            }
            const float x = clip.x / clip.w;
            const float y = clip.y / clip.w;
            const ImVec2 anchor{viewport->Pos.x + (x * 0.5f + 0.5f) * viewport->Size.x,
                                viewport->Pos.y + (0.5f - y * 0.5f) * viewport->Size.y};
            draw_list->AddText(
                anchor,
                ImGui::ColorConvertFloat4ToU32(ImVec4{label.color.x, label.color.y, label.color.z, 1.0f}),
                label.text.c_str());
        }
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
