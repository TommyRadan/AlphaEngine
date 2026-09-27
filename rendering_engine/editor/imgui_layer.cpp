// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/editor/imgui_layer.hpp>

#ifdef ALPHAENGINE_HAS_IMGUI

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/settings.hpp>
#include <core/time.hpp>
#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/editor/helper.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_resources.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu_profiler.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/lighting/spot_light.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/post_settings.hpp>
#include <rendering_engine/render_stats.hpp>
#include <rendering_engine/renderables/model.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>
#include <runtime/scene_manager.hpp>
#include <SDL3/SDL.h>

namespace rendering_engine::editor
{
    namespace
    {
        // Which renderer backend the overlay is driving. Stays @c none
        // until @ref init succeeds, so every entry point early-outs when
        // ImGui is not live.
        enum class backend_mode
        {
            none,
            opengl,
            vulkan,
        };

        backend_mode g_backend = backend_mode::none;

        // Set by begin_frame() once ImGui::Render() has produced draw
        // data, cleared after the draw data is recorded in the debug
        // pass. Guards against recording a half-built frame.
        bool g_frame_ready = false;

        // The VkRenderPass the ImGui Vulkan pipeline was last built
        // for. The debug pass's own render pass can be retired and
        // rebuilt independently of a swapchain change (any acquire
        // through vk_device::acquire_render_pass may hand back a
        // different object); record_draw_data reads the pass it is
        // actually recording into off the encoder
        // (gpu::render_pass_encoder::native_render_pass) and rebuilds
        // the pipeline whenever that differs from this, rather than
        // re-deriving the pass's load/store arguments and guessing they
        // still match what the debug pass begins.
        VkRenderPass g_vulkan_render_pass = VK_NULL_HANDLE;

        // Visibility toggles for the optional panels, driven from the
        // FPS overlay's right-click context menu.
        bool g_show_settings = true;
        bool g_show_profiler = true;
        bool g_show_demo = false;
        bool g_show_helpers = true;
        bool g_show_scene = true;
        bool g_show_post = true;
        bool g_show_render_targets = true;
        bool g_show_console = true;
        bool g_show_hierarchy = true;
        bool g_show_inspector = true;

        // Rolling frame-time history (milliseconds) for the profiler
        // graph, used as a ring buffer.
        constexpr int k_frame_history = 120;
        std::array<float, k_frame_history> g_frame_times{};
        int g_frame_cursor = 0;

        // Edit buffer of the Post panel's colour-grading LUT path. It
        // mirrors the live path while the field is not being edited, and
        // an edit is applied when committed with Enter.
        std::array<char, 512> g_grading_lut_input{};
        bool g_grading_lut_editing = false;

        // Degree <-> radian conversion for the transform / camera / spot
        // light angle fields, which the engine stores in radians but are
        // far more legible to edit in degrees.
        constexpr float k_rad_to_deg = 57.295779513082320876798154814105f;
        constexpr float k_deg_to_rad = 0.017453292519943295769236907684886f;

        // The scene-graph node currently selected in the Hierarchy panel
        // and shown by the Inspector panel, or null. Validated once per
        // frame (see validate_selection) against the live scene forest
        // before the Inspector dereferences it, so a node destroyed since
        // it was selected is never read.
        runtime::node* g_selected_node = nullptr;

        // Transform gizmo (#220) state, shared between the Inspector's mode
        // toggle, the W/E/R shortcuts and the gizmo drawn over the viewport
        // so all three agree on what is currently shown.
        ImGuizmo::OPERATION g_gizmo_operation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE g_gizmo_mode = ImGuizmo::WORLD;
        bool g_gizmo_snap_enabled = false;
        float g_gizmo_snap_translate = 1.0f;
        float g_gizmo_snap_rotate_degrees = 15.0f;
        float g_gizmo_snap_scale = 0.1f;

        void check_vk_result(VkResult result)
        {
            if (result != VK_SUCCESS)
            {
                LOG_ERR("editor: Vulkan error in ImGui backend (VkResult=%d)", static_cast<int>(result));
            }
        }

        // -- Render-target viewer: backend texture-id bridge -----------------
        //
        // ImGui::Image wants an ImTextureID: on OpenGL that is just the
        // texture name, but Vulkan needs a VkDescriptorSet registered
        // through ImGui_ImplVulkan_AddTexture. Every off-screen texture in
        // this engine is transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        // immediately on creation and rests there between render passes
        // (see vk_device::create_texture), so that layout is always the
        // right one to bake into the descriptor — including on the very
        // first frame, before any pass has touched the texture.
        struct vulkan_texture_binding
        {
            VkImageView view{VK_NULL_HANDLE};
            VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
        };

        // Cached per gpu::texture (keyed by its handle id) so a texture
        // sampled by several panel frames in a row reuses one descriptor
        // set; rebuilt when the texture is recreated (its image view
        // changes, e.g. on a resize) and pruned once nothing asks for it
        // any more (see prune_vulkan_texture_bindings), so a window
        // resize does not leak a descriptor set per resize event.
        std::unordered_map<uint64_t, vulkan_texture_binding> g_vulkan_texture_bindings;

        // Immediate release: only safe once nothing can still be reading
        // the descriptor set, i.e. with the queue already idle (see
        // clear_vulkan_texture_bindings).
        void release_vulkan_texture_binding(vulkan_texture_binding& binding)
        {
            if (binding.descriptor_set != VK_NULL_HANDLE)
            {
                ImGui_ImplVulkan_RemoveTexture(binding.descriptor_set);
            }
            binding = vulkan_texture_binding{};
        }

        // Same, but for a binding dropped mid-run (a stale render-target
        // texture the panel no longer shows, or one rebuilt because the
        // texture it names was recreated by a resize): the draw data of a
        // frame or two still in flight may reference the descriptor set
        // through ImGui_ImplVulkan_RenderDrawData, so freeing it right
        // here — as release_vulkan_texture_binding does — races the GPU
        // once several frames are in flight (VUID-vkFreeDescriptorSets-
        // pDescriptorSets-00309). ImGui_ImplVulkan_RemoveTexture runs
        // instead through the device's own deferred-destroy queue, gated
        // on the same submission serial every other Vulkan resource is.
        void defer_release_vulkan_texture_binding(vulkan_texture_binding& binding)
        {
            if (binding.descriptor_set != VK_NULL_HANDLE)
            {
                auto* device = static_cast<gpu::backend::vulkan::vk_device*>(runtime::current_engine().gpu.get());
                const VkDescriptorSet descriptor_set = binding.descriptor_set;
                device->enqueue_destroy([descriptor_set] { ImGui_ImplVulkan_RemoveTexture(descriptor_set); });
            }
            binding = vulkan_texture_binding{};
        }

        // Called once, at shutdown, after the queue has already been
        // waited idle (see editor::shutdown): nothing can still be
        // reading the descriptor sets, so releasing them immediately —
        // ahead of ImGui_ImplVulkan_Shutdown reclaiming the pool they
        // came from — is safe.
        void clear_vulkan_texture_bindings()
        {
            for (auto& [id, binding] : g_vulkan_texture_bindings)
            {
                release_vulkan_texture_binding(binding);
            }
            g_vulkan_texture_bindings.clear();
        }

        // Drops every cached binding whose handle id is not in @p touched.
        // Runs every frame the viewer is active, so a dropped binding may
        // still be drawn by a frame or two in flight; the release defers.
        void prune_vulkan_texture_bindings(const std::vector<uint64_t>& touched)
        {
            for (auto it = g_vulkan_texture_bindings.begin(); it != g_vulkan_texture_bindings.end();)
            {
                if (std::find(touched.begin(), touched.end(), it->first) == touched.end())
                {
                    defer_release_vulkan_texture_binding(it->second);
                    it = g_vulkan_texture_bindings.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // A texture resolved for ImGui::Image: the backend id (0 /
        // ImTextureID_Invalid when @p handle could not be resolved) plus
        // its pixel size, so the caller can preserve the aspect ratio.
        struct resolved_texture
        {
            ImTextureID id{0};
            uint32_t width{0};
            uint32_t height{0};
        };

        resolved_texture resolve_texture(gpu::texture handle)
        {
            resolved_texture result{};
            if (!handle.valid())
            {
                return result;
            }

            if (g_backend == backend_mode::opengl)
            {
                auto* device = static_cast<gpu::backend::opengl::gl_device*>(runtime::current_engine().gpu.get());
                gpu::backend::opengl::gl_texture* tex = device->lookup_texture(handle);
                if (tex == nullptr)
                {
                    return result;
                }
                result.id = static_cast<ImTextureID>(tex->object_id);
                result.width = tex->width;
                result.height = tex->height;
                return result;
            }

            auto* device = static_cast<gpu::backend::vulkan::vk_device*>(runtime::current_engine().gpu.get());
            gpu::backend::vulkan::vk_texture* tex = device->lookup_texture(handle);
            if (tex == nullptr || tex->view == VK_NULL_HANDLE)
            {
                return result;
            }
            result.width = tex->width;
            result.height = tex->height;

            vulkan_texture_binding& binding = g_vulkan_texture_bindings[handle.id];
            if (binding.descriptor_set != VK_NULL_HANDLE && binding.view != tex->view)
            {
                defer_release_vulkan_texture_binding(binding);
            }
            if (binding.descriptor_set == VK_NULL_HANDLE)
            {
                binding.view = tex->view;
                binding.descriptor_set =
                    ImGui_ImplVulkan_AddTexture(tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            result.id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(binding.descriptor_set));
            return result;
        }

        // One texture the viewer can show, paired with the label it lists
        // it under. Built fresh every frame from the renderer's read-only
        // accessors, so a target that comes and goes (temporal AA off, no
        // environment set) appears and disappears with it.
        struct render_target_slot
        {
            const char* label;
            gpu::texture texture;
        };

        // ImGui::Image over the engine's intermediate textures: the HDR
        // scene colour, scene depth, the tonemapped LDR image, the spot
        // shadow map, the velocity buffer, the temporal-AA resolve and
        // the active environment's BRDF LUT. Every off-screen render
        // target the engine currently exposes as a plain 2D, colour-or-
        // depth texture ends up here; the ones that do not fit that shape
        // (the directional light's cascade array, the point-light cube
        // shadow map, the prefiltered / irradiance IBL cubes) are listed
        // but not shown — ImGui's image shader has no array- or
        // cube-sampling path.
        void draw_render_targets_window()
        {
            if (!g_show_render_targets)
            {
                return;
            }

            auto& renderer = *runtime::current_engine().renderer;
            const std::array<render_target_slot, 7> slots = {{
                {"HDR scene colour", renderer.scene_color_texture()},
                {"Scene depth", renderer.scene_depth_texture()},
                {"LDR colour (tonemapped)", renderer.ldr_color_texture()},
                {"Spot shadow map", renderer.spot_shadow_map()},
                {"Velocity", renderer.velocity_texture()},
                {"TAA resolve", renderer.taa_resolve_texture()},
                {"IBL BRDF LUT", renderer.environment_brdf_lut()},
            }};

            // Resolved (and, on Vulkan, cached / pruned) every frame
            // regardless of whether the window is open or collapsed, so
            // the descriptor cache always tracks exactly the handles
            // currently in play rather than whatever was last drawn.
            std::vector<uint64_t> touched;
            std::array<resolved_texture, slots.size()> resolved{};
            for (std::size_t i = 0; i < slots.size(); ++i)
            {
                if (slots[i].texture.valid())
                {
                    resolved[i] = resolve_texture(slots[i].texture);
                    touched.push_back(slots[i].texture.id);
                }
            }
            if (g_backend == backend_mode::vulkan)
            {
                prune_vulkan_texture_bindings(touched);
            }

            ImGui::SetNextWindowSize(ImVec2{420.0f, 380.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Render Targets", &g_show_render_targets))
            {
                static int selected = 0;
                const render_target_slot& current = slots[static_cast<std::size_t>(selected)];
                if (ImGui::BeginCombo("##render_target_select", current.label))
                {
                    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
                    {
                        const bool is_selected = (selected == i);
                        if (ImGui::Selectable(slots[static_cast<std::size_t>(i)].label, is_selected))
                        {
                            selected = i;
                        }
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                const resolved_texture& shown = resolved[static_cast<std::size_t>(selected)];
                if (shown.id == 0)
                {
                    ImGui::TextDisabled("not active");
                }
                else
                {
                    const float aspect =
                        shown.height > 0 ? static_cast<float>(shown.width) / static_cast<float>(shown.height) : 1.0f;
                    const float width = ImGui::GetContentRegionAvail().x;
                    ImGui::Image(shown.id, ImVec2{width, aspect > 0.0f ? width / aspect : width});
                    ImGui::Text("%u x %u", shown.width, shown.height);
                }

                ImGui::Separator();
                ImGui::TextDisabled("Not shown: directional shadow (cascade array), point-light shadow (cube),\n"
                                    "prefiltered / irradiance IBL (cube)");
            }
            ImGui::End();
        }

        // -- Console -----------------------------------------------------

        int g_console_level_filter = static_cast<int>(core::logging::verbosity::trace);
        char g_console_category_filter[64] = {};
        bool g_console_auto_scroll = true;

        ImVec4 console_level_color(core::logging::verbosity level)
        {
            switch (level)
            {
            case core::logging::verbosity::trace:
                return ImVec4{0.55f, 0.55f, 0.55f, 1.0f};
            case core::logging::verbosity::debug:
                return ImVec4{0.70f, 0.75f, 0.95f, 1.0f};
            case core::logging::verbosity::info:
                return ImVec4{0.85f, 0.85f, 0.85f, 1.0f};
            case core::logging::verbosity::warn:
                return ImVec4{0.95f, 0.75f, 0.20f, 1.0f};
            case core::logging::verbosity::error:
                return ImVec4{0.95f, 0.35f, 0.35f, 1.0f};
            case core::logging::verbosity::fatal:
                return ImVec4{1.00f, 0.20f, 0.20f, 1.0f};
            }
            return ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
        }

        std::string format_timestamp(std::chrono::system_clock::time_point timestamp)
        {
            const std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
            std::tm local{};
            if (!core::platform::local_time(time, local))
            {
                return "--:--:--";
            }
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
            return buffer;
        }

        // The log ring buffer (#195) with a minimum-level and a
        // category-substring filter, auto-scroll and a clear button.
        void draw_console_window()
        {
            if (!g_show_console)
            {
                return;
            }

            ImGui::SetNextWindowSize(ImVec2{560.0f, 280.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Console", &g_show_console))
            {
                static constexpr std::array<const char*, 6> level_names = {
                    "Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
                ImGui::SetNextItemWidth(90.0f);
                ImGui::Combo(
                    "##min_level", &g_console_level_filter, level_names.data(), static_cast<int>(level_names.size()));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputTextWithHint("##category_filter",
                                         "category filter",
                                         g_console_category_filter,
                                         sizeof(g_console_category_filter));
                ImGui::SameLine();
                ImGui::Checkbox("Auto-scroll", &g_console_auto_scroll);
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear"))
                {
                    core::logging::clear_recent_messages();
                }
                ImGui::Separator();

                if (ImGui::BeginChild(
                        "##console_scroll", ImVec2{0.0f, 0.0f}, false, ImGuiWindowFlags_HorizontalScrollbar))
                {
                    for (const core::logging::record& record : core::logging::recent_messages())
                    {
                        if (static_cast<int>(record.level) < g_console_level_filter)
                        {
                            continue;
                        }
                        if (g_console_category_filter[0] != '\0' &&
                            record.category.find(g_console_category_filter) == std::string::npos)
                        {
                            continue;
                        }
                        const std::string line =
                            format_timestamp(record.timestamp) + " [" + record.category + "] " + record.text;
                        ImGui::PushStyleColor(ImGuiCol_Text, console_level_color(record.level));
                        ImGui::TextUnformatted(line.c_str());
                        ImGui::PopStyleColor();
                    }
                    if (g_console_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                    {
                        ImGui::SetScrollHereY(1.0f);
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }

        // -- Hierarchy + Inspector --------------------------------------------

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

        // Clears g_selected_node once its node is no longer reachable
        // from any loaded scene's root, so the Inspector never reads a
        // freed node. Run once per frame regardless of which (if any)
        // debug panel is visible.
        void validate_selection()
        {
            if (g_selected_node == nullptr)
            {
                return;
            }
            auto& scenes = *runtime::current_engine().scenes;
            for (std::size_t i = 0; i < scenes.scene_count(); ++i)
            {
                if (node_exists(scenes.scene_at(i).root, g_selected_node))
                {
                    return;
                }
            }
            g_selected_node = nullptr;
        }

        // One row of the tree, recursing into children when expanded.
        void draw_node_row(runtime::node& node)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            const bool is_leaf = node.children().empty();
            if (is_leaf)
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (&node == g_selected_node)
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
                g_selected_node = &node;
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
        void draw_hierarchy_window()
        {
            if (!g_show_hierarchy)
            {
                return;
            }

            auto& scenes = *runtime::current_engine().scenes;

            ImGui::SetNextWindowSize(ImVec2{280.0f, 380.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Hierarchy", &g_show_hierarchy))
            {
                for (std::size_t i = 0; i < scenes.scene_count(); ++i)
                {
                    runtime::scene& scene = scenes.scene_at(i);
                    const char* name = scenes.name_at(i).c_str();
                    ImGui::PushID(static_cast<int>(i));
                    const bool open =
                        ImGui::TreeNodeEx(name[0] != '\0' ? name : "scene",
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

        // Mode toggle and optional snapping for the transform gizmo (#220)
        // drawn over the viewport by draw_gizmo. The W/E/R shortcuts
        // (handle_gizmo_shortcuts) change the same g_gizmo_operation, so
        // the radio buttons here always reflect whichever one fired last.
        void draw_gizmo_controls()
        {
            ImGui::SeparatorText("Gizmo");
            if (ImGui::RadioButton("Translate (W)", g_gizmo_operation == ImGuizmo::TRANSLATE))
            {
                g_gizmo_operation = ImGuizmo::TRANSLATE;
            }
            if (ImGui::RadioButton("Rotate (E)", g_gizmo_operation == ImGuizmo::ROTATE))
            {
                g_gizmo_operation = ImGuizmo::ROTATE;
            }
            if (ImGui::RadioButton("Scale (R)", g_gizmo_operation == ImGuizmo::SCALE))
            {
                g_gizmo_operation = ImGuizmo::SCALE;
            }

            // ImGuizmo always manipulates SCALE in local space regardless of
            // this setting, so the toggle is hidden while it would do nothing.
            if (g_gizmo_operation != ImGuizmo::SCALE)
            {
                if (ImGui::RadioButton("Local##gizmo_space", g_gizmo_mode == ImGuizmo::LOCAL))
                {
                    g_gizmo_mode = ImGuizmo::LOCAL;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("World##gizmo_space", g_gizmo_mode == ImGuizmo::WORLD))
                {
                    g_gizmo_mode = ImGuizmo::WORLD;
                }
            }

            ImGui::Checkbox("Snap##gizmo", &g_gizmo_snap_enabled);
            switch (g_gizmo_operation)
            {
            case ImGuizmo::ROTATE:
                ImGui::DragFloat("Snap degrees##gizmo", &g_gizmo_snap_rotate_degrees, 1.0f, 0.0f, 180.0f);
                break;
            case ImGuizmo::SCALE:
                ImGui::DragFloat("Snap scale##gizmo", &g_gizmo_snap_scale, 0.01f, 0.01f, 10.0f);
                break;
            default:
                ImGui::DragFloat("Snap step##gizmo", &g_gizmo_snap_translate, 0.05f, 0.01f, 100.0f);
                break;
            }
        }

        // The selected node's name, active flag and transform, plus a
        // hand-written section per built-in component it carries. Kept
        // here in the editor layer rather than on the components so release
        // builds carry none of this.
        void draw_inspector_window()
        {
            if (!g_show_inspector)
            {
                return;
            }

            ImGui::SetNextWindowSize(ImVec2{320.0f, 420.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Inspector", &g_show_inspector))
            {
                if (g_selected_node == nullptr)
                {
                    ImGui::TextDisabled("no node selected");
                }
                else
                {
                    runtime::node& node = *g_selected_node;

                    static runtime::node* last_node = nullptr;
                    static char name_buffer[128];
                    if (last_node != &node)
                    {
                        last_node = &node;
                        std::snprintf(name_buffer, sizeof(name_buffer), "%s", node.name().c_str());
                    }
                    ImGui::InputText("Name", name_buffer, sizeof(name_buffer));
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        node.set_name(name_buffer);
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

        // -- Default docking layout -------------------------------------------

        // Arranges the built-in panels into a hierarchy / inspector /
        // render-targets / console layout around a central, transparent
        // dock node (so the game view shows through it). Only called once,
        // the first time this dockspace id has no node — a fresh
        // imgui.ini, or one predating this dockspace — so a user's own
        // rearrangement, once saved, is never overwritten.
        void build_default_dock_layout(ImGuiID dockspace_id)
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            // ImGuiDockNodeFlags_DockSpace is the internal-only flag
            // DockBuilderAddNode expects for a dockspace root; the cast
            // avoids an enum-mismatch warning ORing it with the public
            // ImGuiDockNodeFlags_PassthruCentralNode.
            ImGui::DockBuilderAddNode(dockspace_id,
                                      static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
                                          ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            ImGuiID main_id = dockspace_id;
            ImGuiID right_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.26f, nullptr, &main_id);
            ImGuiID bottom_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.28f, nullptr, &main_id);
            ImGuiID left_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Left, 0.2f, nullptr, &main_id);
            ImGuiID right_bottom_id = ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Down, 0.5f, nullptr, &right_id);

            ImGui::DockBuilderDockWindow("Hierarchy", left_id);
            ImGui::DockBuilderDockWindow("Inspector", right_id);
            ImGui::DockBuilderDockWindow("Render Targets", right_bottom_id);
            ImGui::DockBuilderDockWindow("Console", bottom_id);
            ImGui::DockBuilderDockWindow("Profiler", main_id);
            ImGui::DockBuilderDockWindow("Scene", main_id);
            ImGui::DockBuilderDockWindow("Settings", main_id);
            ImGui::DockBuilderDockWindow("Post", main_id);
            ImGui::DockBuilderDockWindow("Helpers", main_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }

        // Hosts a full-viewport, passthrough dockspace so every panel can
        // dock against the window edges and against each other. Built
        // once with a stable id so ImGui's own ini persistence
        // (core::platform::pref_path, set up in init) remembers whatever
        // arrangement the user leaves it in across runs. Returns the
        // dockspace id so the caller can locate the central node — the
        // passthrough game view the transform gizmo draws over.
        ImGuiID setup_dockspace()
        {
            const ImGuiID dockspace_id = ImGui::GetID("AlphaEngineDockSpace");
            if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
            {
                build_default_dock_layout(dockspace_id);
            }
            ImGui::DockSpaceOverViewport(
                dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
            return dockspace_id;
        }

        // -- Transform gizmo (#220) -------------------------------------------

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

        // W/E/R cycle the gizmo between translate / rotate / scale, matching
        // the Inspector's radio buttons (draw_gizmo_controls). Checked every
        // frame regardless of whether a node is selected, same as the
        // Inspector toggle.
        void handle_gizmo_shortcuts()
        {
            if (!viewport_has_focus())
            {
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W))
            {
                g_gizmo_operation = ImGuizmo::TRANSLATE;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_E))
            {
                g_gizmo_operation = ImGuizmo::ROTATE;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_R))
            {
                g_gizmo_operation = ImGuizmo::SCALE;
            }
        }

        // Translate / rotate / scale gizmo on the Hierarchy's current
        // selection, drawn over @p dockspace_id's central node (the
        // passthrough game view) in the active camera's view and
        // projection. ImGuizmo::Manipulate always works in world space —
        // g_gizmo_mode only orients the translate/rotate handles, and it
        // ignores the mode entirely for scale — so the manipulated result
        // is converted back to the node's local transform by undoing its
        // parent's world matrix (a root node's local and world already
        // agree). A node whose pose is driven by physics or an animator is
        // simply overwritten again next frame, same as any other manual
        // edit to its transform.
        void draw_gizmo(ImGuiID dockspace_id)
        {
            if (g_selected_node == nullptr)
            {
                return;
            }
            const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace_id);
            rendering_engine::camera* cam = rendering_engine::active_camera();
            if (central == nullptr || cam == nullptr)
            {
                return;
            }

            ImGuizmo::SetOrthographic(dynamic_cast<rendering_engine::orthographic_camera*>(cam) != nullptr);
            ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
            ImGuizmo::SetRect(central->Pos.x, central->Pos.y, central->Size.x, central->Size.y);

            runtime::node& node = *g_selected_node;
            core::math::mat4 world = node.world_matrix();
            const core::math::mat4 view = cam->get_view_matrix();
            const core::math::mat4 projection = cam->get_projection_matrix();

            float snap[3] = {g_gizmo_snap_translate, g_gizmo_snap_translate, g_gizmo_snap_translate};
            if (g_gizmo_operation == ImGuizmo::ROTATE)
            {
                snap[0] = snap[1] = snap[2] = g_gizmo_snap_rotate_degrees;
            }
            else if (g_gizmo_operation == ImGuizmo::SCALE)
            {
                snap[0] = snap[1] = snap[2] = g_gizmo_snap_scale;
            }

            ImGuizmo::Manipulate(view.data(),
                                 projection.data(),
                                 g_gizmo_operation,
                                 g_gizmo_mode,
                                 world.data(),
                                 nullptr,
                                 g_gizmo_snap_enabled ? snap : nullptr);

            if (!ImGuizmo::IsUsing())
            {
                return;
            }

            runtime::node* parent = node.parent();
            const core::math::mat4 local =
                parent != nullptr ? core::math::inverse(parent->world_matrix()) * world : world;
            const core::math::trs pose = decompose_matrix(local);
            node.transform.set_position(pose.translation);
            node.transform.set_quaternion(pose.rotation);
            node.transform.set_scale(pose.scale);
        }

        // Small always-on overlay pinned to the top-right corner showing
        // the frame rate and frame time. Right-clicking it toggles the
        // heavier inspector panels.
        void draw_fps_overlay()
        {
            const auto& time = *runtime::current_engine().time;

            constexpr float pad = 10.0f;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 work_pos = viewport->WorkPos;
            const ImVec2 work_size = viewport->WorkSize;
            const ImVec2 position{work_pos.x + work_size.x - pad, work_pos.y + pad};
            ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2{1.0f, 0.0f});
            ImGui::SetNextWindowBgAlpha(0.35f);

            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                           ImGuiWindowFlags_NoMove;
            if (ImGui::Begin("fps_overlay", nullptr, flags))
            {
                ImGui::Text("FPS: %.0f", static_cast<double>(time.current_fps()));
                ImGui::Text("Frame: %.2f ms", time.delta_time());
                ImGui::Separator();
                ImGui::TextDisabled("right-click for tools");
                if (ImGui::BeginPopupContextWindow())
                {
                    ImGui::MenuItem("Render Targets", nullptr, &g_show_render_targets);
                    ImGui::MenuItem("Console", nullptr, &g_show_console);
                    ImGui::MenuItem("Hierarchy", nullptr, &g_show_hierarchy);
                    ImGui::MenuItem("Inspector", nullptr, &g_show_inspector);
                    ImGui::MenuItem("Profiler", nullptr, &g_show_profiler);
                    ImGui::MenuItem("Scene", nullptr, &g_show_scene);
                    ImGui::MenuItem("Settings", nullptr, &g_show_settings);
                    ImGui::MenuItem("Post", nullptr, &g_show_post);
                    ImGui::MenuItem("Helpers", nullptr, &g_show_helpers);
                    ImGui::MenuItem("ImGui demo", nullptr, &g_show_demo);
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }

        // Frame-time profiler: plots the rolling history and reports the
        // min / max / average over the window so a hitch is visible at a
        // glance.
        void draw_profiler_window()
        {
            if (!g_show_profiler)
            {
                return;
            }

            ImGui::SetNextWindowSize(ImVec2{320.0f, 140.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Profiler", &g_show_profiler))
            {
                float min_ms = g_frame_times[0];
                float max_ms = g_frame_times[0];
                float sum_ms = 0.0f;
                for (const float sample : g_frame_times)
                {
                    min_ms = sample < min_ms ? sample : min_ms;
                    max_ms = sample > max_ms ? sample : max_ms;
                    sum_ms += sample;
                }
                const float avg_ms = sum_ms / static_cast<float>(k_frame_history);

                char overlay[64];
                std::snprintf(overlay,
                              sizeof(overlay),
                              "avg %.2f ms (%.0f fps)",
                              static_cast<double>(avg_ms),
                              avg_ms > 0.0f ? 1000.0 / static_cast<double>(avg_ms) : 0.0);
                // Upper bound of the plot tracks the worst recent frame so
                // spikes stay on-scale; clamp to a sane floor.
                const float scale_max = max_ms > 1.0f ? max_ms * 1.2f : 1.2f;
                ImGui::PlotLines("##frame_times",
                                 g_frame_times.data(),
                                 k_frame_history,
                                 g_frame_cursor,
                                 overlay,
                                 0.0f,
                                 scale_max,
                                 ImVec2{0.0f, 70.0f});
                ImGui::Text("min %.2f ms", static_cast<double>(min_ms));
                ImGui::SameLine();
                ImGui::Text("max %.2f ms", static_cast<double>(max_ms));

                // GPU time per pass from the device's
                // timestamp queries (last resolved frame).
                ImGui::SeparatorText("GPU");
                const gpu_profiler& profiler = runtime::current_engine().renderer->get_gpu_profiler();
                if (!profiler.enabled())
                {
                    ImGui::TextDisabled("no timestamp queries on this device");
                }
                else
                {
                    ImGui::Text("frame %.2f ms", static_cast<double>(profiler.frame_gpu_ms()));
                    if (ImGui::BeginTable("gpu_passes", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        for (const gpu_pass_timing& timing : profiler.timings())
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(timing.name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.3f ms", static_cast<double>(timing.gpu_ms));
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::End();
        }

        // Scene statistics: how much geometry the scene pass submitted last
        // frame (models, draw calls, instances, triangles, vertices) plus a
        // breakdown of the live lights by type. The geometry figures come
        // from the renderer's per-frame @ref render_stats; the light counts
        // are read live from the lighting registry.
        void draw_scene_window()
        {
            if (!g_show_scene)
            {
                return;
            }

            const auto& stats = runtime::current_engine().renderer->get_render_stats();

            uint32_t ambient = 0;
            uint32_t directional = 0;
            uint32_t point = 0;
            uint32_t spot = 0;
            const auto& lights = registered_lights();
            for (const auto* source : lights)
            {
                switch (source->type())
                {
                case light_type::ambient:
                    ++ambient;
                    break;
                case light_type::directional:
                    ++directional;
                    break;
                case light_type::point:
                    ++point;
                    break;
                case light_type::spot:
                    ++spot;
                    break;
                }
            }

            ImGui::SetNextWindowSize(ImVec2{300.0f, 0.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Scene", &g_show_scene))
            {
                ImGui::SeparatorText("Geometry");
                ImGui::Text("Models in scene: %u", stats.scene_renderables);
                ImGui::Text("Models rendered: %u", stats.submitted);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Renderables whose bounds touch the camera frustum, plus any that report\n"
                                      "no bounds (fullscreen effects, gizmos) and are always drawn.");
                }
                ImGui::Text("Frustum culled: %u", stats.culled);
                ImGui::Text("Shadow culled: %u (omni %u, spot %u)",
                            stats.shadow_culled,
                            stats.point_shadow_culled,
                            stats.spot_shadow_culled);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Caster/cascade pairs skipped across the directional shadow cascades,\n"
                                      "caster/face pairs skipped across the six omni shadow faces, and\n"
                                      "casters skipped by the spot shadow pass.");
                }
                ImGui::Text("Draw calls: %u", stats.draw_calls);
                ImGui::Text("Instances: %u", stats.instances);
                ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(stats.triangles));
                ImGui::Text("Lines: %llu  Points: %llu",
                            static_cast<unsigned long long>(stats.lines),
                            static_cast<unsigned long long>(stats.points));
                ImGui::Text("Vertices: %llu", static_cast<unsigned long long>(stats.vertices));

                ImGui::SeparatorText("Lights");
                ImGui::Text("Total: %zu", lights.size());
                ImGui::Text("Ambient: %u", ambient);
                ImGui::Text("Directional: %u", directional);
                ImGui::Text("Point: %u", point);
                ImGui::Text("Spot: %u", spot);
            }
            ImGui::End();
        }

        // Read-only inspector for the engine-wide settings object. The
        // accessors are const, so the panel reports the resolved
        // configuration rather than editing it.
        void draw_settings_window()
        {
            if (!g_show_settings)
            {
                return;
            }

            const auto& settings = *runtime::current_engine().settings;
            ImGui::SetNextWindowSize(ImVec2{320.0f, 0.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Settings", &g_show_settings))
            {
                ImGui::SeparatorText("Window");
                ImGui::Text("Size: %u x %u", settings.window.width, settings.window.height);
                ImGui::Text("Aspect: %.3f", static_cast<double>(settings.window.aspect_ratio()));
                ImGui::Text("Mode: %s", core::window_mode_name(settings.window.mode));
                ImGui::Text("Double buffered: %s", settings.window.double_buffered ? "yes" : "no");
                ImGui::Text("Vsync: %s", settings.window.vsync ? "on" : "off");

                ImGui::SeparatorText("Camera / input");
                ImGui::Text("Field of view: %.1f", static_cast<double>(settings.camera.field_of_view));
                ImGui::Text("Mouse sensitivity: %.4f", static_cast<double>(settings.input.mouse_sensitivity));
                ImGui::Text("Mouse reversed: %s", settings.input.mouse_reversed ? "yes" : "no");

                ImGui::SeparatorText("GPU");
                ImGui::Text("Backend: %s", core::graphics_backend_name(settings.graphics.backend));
            }
            ImGui::End();
        }

        // Runtime tuning for the post-processing chain: tonemap
        // exposure/operator, auto exposure, colour grading, volumetric fog,
        // motion blur, bloom, temporal AA feedback and FXAA. Every edit is
        // written back through renderer::set_post_settings, the same path
        // any other caller would use, so it takes effect on the next
        // recorded frame (tonemap immediately, since its setters rewrite
        // their UBO on the spot). TAA's own enabled checkbox is shown
        // disabled: the pass is only ever brought up once, at init, from
        // graphics.temporal_aa (see post_settings::taa's doc comment).
        // The panel edits the live settings only: core::settings has no
        // save path, so the startup values stay whatever settings.json,
        // the environment and the command line resolved.
        void draw_post_window()
        {
            if (!g_show_post)
            {
                return;
            }

            auto& renderer = *runtime::current_engine().renderer;
            rendering_engine::post_settings settings = renderer.get_post_settings();
            bool changed = false;

            ImGui::SetNextWindowSize(ImVec2{300.0f, 0.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Post", &g_show_post))
            {
                ImGui::SeparatorText("Tonemap");
                ImGui::BeginDisabled(settings.auto_exposure.enabled);
                changed |= ImGui::SliderFloat("Exposure", &settings.exposure, 0.0f, 8.0f);
                ImGui::EndDisabled();
                if (settings.auto_exposure.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("Driven by auto exposure; tune its compensation instead");
                }
                static constexpr std::array<const char*, 3> operator_names = {"None", "Reinhard", "ACES"};
                int op = static_cast<int>(settings.tonemap_op);
                if (ImGui::Combo("Operator", &op, operator_names.data(), static_cast<int>(operator_names.size())))
                {
                    settings.tonemap_op = static_cast<rendering_engine::tonemap_operator>(op);
                    changed = true;
                }

                ImGui::SeparatorText("Auto exposure");
                changed |= ImGui::Checkbox("Enabled##auto_exposure", &settings.auto_exposure.enabled);
                changed |= ImGui::SliderFloat("Min EV100", &settings.auto_exposure.min_ev, -16.0f, 32.0f);
                changed |= ImGui::SliderFloat("Max EV100", &settings.auto_exposure.max_ev, -16.0f, 32.0f);
                changed |= ImGui::SliderFloat("Speed up", &settings.auto_exposure.speed_up, 0.0f, 10.0f);
                changed |= ImGui::SliderFloat("Speed down", &settings.auto_exposure.speed_down, 0.0f, 10.0f);
                changed |= ImGui::SliderFloat("Compensation", &settings.auto_exposure.compensation, -8.0f, 8.0f);

                ImGui::SeparatorText("Colour grading");
                // Mirror the live path unless the field is being edited, so
                // a path set elsewhere shows up and a half-typed one is not
                // clobbered; the edit applies once committed with Enter.
                if (!g_grading_lut_editing)
                {
                    std::snprintf(
                        g_grading_lut_input.data(), g_grading_lut_input.size(), "%s", settings.grading.lut.c_str());
                }
                if (ImGui::InputText("LUT",
                                     g_grading_lut_input.data(),
                                     g_grading_lut_input.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    settings.grading.lut = g_grading_lut_input.data();
                    changed = true;
                }
                g_grading_lut_editing = ImGui::IsItemActive();
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("N*N x N strip LUT image; empty turns grading off (Enter applies)");
                }
                changed |= ImGui::SliderFloat("Intensity##grading", &settings.grading.intensity, 0.0f, 1.0f);
                if (settings.grading.lut.empty())
                {
                    ImGui::TextDisabled("No LUT: grading off");
                }
                else if (!renderer.grading_lut_loaded())
                {
                    ImGui::TextDisabled("LUT not loaded (see the log)");
                }

                ImGui::SeparatorText("Volumetric fog");
                changed |= ImGui::Checkbox("Enabled##volumetric", &settings.volumetric.enabled);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Raymarches the scene's height fog: needs fog_settings::height_density > 0");
                }
                changed |= ImGui::SliderFloat("Density scale", &settings.volumetric.density_scale, 0.0f, 4.0f);
                changed |= ImGui::SliderFloat("Anisotropy", &settings.volumetric.anisotropy, -0.95f, 0.95f);
                changed |= ImGui::SliderFloat("Max distance", &settings.volumetric.max_distance, 1.0f, 256.0f);
                changed |= ImGui::SliderInt("Steps", &settings.volumetric.steps, 4, 128);
                changed |= ImGui::SliderFloat("Intensity", &settings.volumetric.intensity, 0.0f, 8.0f);

                ImGui::SeparatorText("Motion blur");
                changed |= ImGui::Checkbox("Enabled##motion_blur", &settings.motion_blur.enabled);
                changed |= ImGui::SliderFloat("Shutter##motion_blur", &settings.motion_blur.intensity, 0.0f, 2.0f);
                changed |= ImGui::SliderInt("Samples##motion_blur", &settings.motion_blur.samples, 2, 32);
                changed |=
                    ImGui::SliderFloat("Max radius (px)##motion_blur", &settings.motion_blur.max_radius, 1.0f, 128.0f);

                ImGui::SeparatorText("Bloom");
                changed |= ImGui::Checkbox("Enabled##bloom", &settings.bloom.enabled);
                changed |= ImGui::SliderFloat("Threshold", &settings.bloom.threshold, 0.0f, 4.0f);
                changed |= ImGui::SliderFloat("Knee", &settings.bloom.knee, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Strength", &settings.bloom.strength, 0.0f, 2.0f);

                ImGui::SeparatorText("Temporal AA");
                bool taa_enabled = settings.taa.enabled;
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("Enabled##taa", &taa_enabled);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Fixed at startup by graphics.temporal_aa");
                }
                changed |= ImGui::SliderFloat("Feedback", &settings.taa.feedback, 0.0f, 0.99f);

                ImGui::SeparatorText("FXAA");
                changed |= ImGui::Checkbox("Enabled##fxaa", &settings.fxaa.enabled);
            }
            ImGui::End();

            if (changed)
            {
                renderer.set_post_settings(settings);
            }
        }

        // Lists every live debug gizmo with
        // a checkbox bound to its visibility, plus master show / hide
        // shortcuts. The helper registry is shared with the renderer, so
        // the toggles take effect on the next debug-pass draw.
        void draw_helpers_window()
        {
            if (!g_show_helpers)
            {
                return;
            }

            const auto& helpers = rendering_engine::editor::registered_helpers();

            ImGui::SetNextWindowSize(ImVec2{260.0f, 0.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Helpers", &g_show_helpers))
            {
                if (helpers.empty())
                {
                    ImGui::TextDisabled("no debug helpers registered");
                }
                else
                {
                    if (ImGui::SmallButton("Show all"))
                    {
                        for (auto* gizmo : helpers)
                        {
                            gizmo->visible = true;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Hide all"))
                    {
                        for (auto* gizmo : helpers)
                        {
                            gizmo->visible = false;
                        }
                    }
                    ImGui::Separator();

                    // Disambiguate the checkbox ids by index so two
                    // helpers sharing a name still toggle independently.
                    int index = 0;
                    for (auto* gizmo : helpers)
                    {
                        ImGui::PushID(index++);
                        ImGui::Checkbox(gizmo->name(), &gizmo->visible);
                        ImGui::PopID();
                    }
                }
            }
            ImGui::End();
        }

        void build_panels()
        {
            // Push this frame's time into the rolling history first so the
            // profiler reflects the live cadence.
            const auto& time = *runtime::current_engine().time;
            g_frame_times[g_frame_cursor] = static_cast<float>(time.delta_time());
            g_frame_cursor = (g_frame_cursor + 1) % k_frame_history;

            const ImGuiID dockspace_id = setup_dockspace();
            validate_selection();
            handle_gizmo_shortcuts();

            draw_fps_overlay();
            draw_render_targets_window();
            draw_console_window();
            draw_hierarchy_window();
            draw_inspector_window();
            draw_profiler_window();
            draw_scene_window();
            draw_settings_window();
            draw_post_window();
            draw_helpers_window();
            draw_gizmo(dockspace_id);
            if (g_show_demo)
            {
                ImGui::ShowDemoWindow(&g_show_demo);
            }
        }

        // The render pass the debug pass draws into: swapchain target,
        // colour loaded (the UI/scene already composited), no depth.
        // ImGui builds its pipeline against this pass, so it must match
        // what the debug pass begins. The debug pass leaves depth.load
        // at its default (clear) and acquire_render_pass keys on it even
        // when depth is unused, so the same value is passed here and
        // the cache hands back the very VkRenderPass the pass records
        // into. Null when the device has no swapchain target.
        VkRenderPass acquire_ui_render_pass(gpu::backend::vulkan::vk_device& device)
        {
            const gpu::render_target swapchain = device.swapchain_target();
            auto* target = device.lookup_render_target(swapchain);
            if (target == nullptr)
            {
                LOG_ERR("editor: no swapchain render target for the ImGui Vulkan pipeline");
                return VK_NULL_HANDLE;
            }
            return device.acquire_render_pass(
                *target, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR, /*use_depth=*/false);
        }

        // Rebuild ImGui's main pipeline against the render pass @p encoder
        // is actually recording into, if that differs from the one the
        // pipeline was last built for. Called right before recording,
        // inside the debug pass: the pass only changes when the swapchain
        // was rebuilt, which waited the device idle, so no frame in flight
        // still binds the old pipeline and this frame has not bound it
        // yet, so ImGui may destroy it here. Only the pipeline is rebuilt
        // — the font texture, vertex / index buffers and descriptor pool
        // survive. Returns false when
        // no pipeline could be built (the pass is not open this frame);
        // the caller then skips this frame's overlay.
        bool refresh_vulkan_pipeline(gpu::render_pass_encoder& encoder)
        {
            auto native_pass = static_cast<VkRenderPass>(encoder.native_render_pass());
            if (native_pass == VK_NULL_HANDLE)
            {
                return false;
            }
            if (native_pass == g_vulkan_render_pass)
            {
                return true;
            }
            ImGui_ImplVulkan_PipelineInfo pipeline_info{};
            pipeline_info.RenderPass = native_pass;
            pipeline_info.Subpass = 0;
            pipeline_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            ImGui_ImplVulkan_CreateMainPipeline(&pipeline_info);
            g_vulkan_render_pass = native_pass;
            LOG_INF("editor: ImGui Vulkan pipeline rebuilt for a new debug-pass render pass");
            return true;
        }

        bool init_vulkan(runtime::engine& eng)
        {
            auto* device = static_cast<gpu::backend::vulkan::vk_device*>(eng.gpu.get());

            // Acquire the same render pass the debug pass draws into
            // (see acquire_ui_render_pass) and remember which swapchain
            // it belongs to, so a later rebuild is noticed before the
            // first record against the new one.
            VkRenderPass ui_render_pass = acquire_ui_render_pass(*device);
            if (ui_render_pass == VK_NULL_HANDLE)
            {
                LOG_ERR("editor: acquire_render_pass returned null for ImGui Vulkan init");
                return false;
            }
            g_vulkan_render_pass = ui_render_pass;

            if (!ImGui_ImplSDL3_InitForVulkan(eng.window->sdl_window()))
            {
                LOG_ERR("editor: ImGui_ImplSDL3_InitForVulkan failed");
                return false;
            }

            // ImGui cycles its own host-visible vertex / index buffers
            // through ImageCount sets, one per RenderDrawData call, so the
            // count must cover every frame the device keeps in flight: a
            // set is rewritten only once the frame that read it has
            // retired. The backend also requires at least two.
            const uint32_t image_count = std::max(device->swapchain_image_count(), device->frames_in_flight());
            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion = VK_API_VERSION_1_0;
            init_info.Instance = device->instance();
            init_info.PhysicalDevice = device->physical_device();
            init_info.Device = device->vk_handle();
            init_info.QueueFamily = device->graphics_queue_family();
            init_info.Queue = device->graphics_queue();
            // Leave DescriptorPool null and let the backend own a pool
            // sized for the font atlas and every render-target texture the
            // Render Targets panel registers through
            // ImGui_ImplVulkan_AddTexture (see resolve_texture); avoids
            // depending on the engine pool's descriptor budget / flags.
            init_info.DescriptorPool = VK_NULL_HANDLE;
            init_info.DescriptorPoolSize = 64;
            init_info.MinImageCount = image_count < 2 ? 2 : image_count;
            init_info.ImageCount = image_count < 2 ? 2 : image_count;
            // The device's pipeline cache, so the overlay's pipeline (and
            // its rebuild after every swapchain rebuild) is served from
            // and persisted with the engine's own.
            init_info.PipelineCache = device->pipeline_cache();
            init_info.PipelineInfoMain.RenderPass = ui_render_pass;
            init_info.PipelineInfoMain.Subpass = 0;
            init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering = false;
            init_info.Allocator = nullptr;
            init_info.CheckVkResultFn = check_vk_result;
            if (!ImGui_ImplVulkan_Init(&init_info))
            {
                LOG_ERR("editor: ImGui_ImplVulkan_Init failed");
                ImGui_ImplSDL3_Shutdown();
                return false;
            }

            g_backend = backend_mode::vulkan;
            return true;
        }

        bool init_opengl(runtime::engine& eng)
        {
            if (!ImGui_ImplSDL3_InitForOpenGL(eng.window->sdl_window(), eng.window->gl_context()))
            {
                LOG_ERR("editor: ImGui_ImplSDL3_InitForOpenGL failed");
                return false;
            }
            if (!ImGui_ImplOpenGL3_Init("#version 460"))
            {
                LOG_ERR("editor: ImGui_ImplOpenGL3_Init failed");
                ImGui_ImplSDL3_Shutdown();
                return false;
            }
            g_backend = backend_mode::opengl;
            return true;
        }
        // Where ImGui persists window / dock layout (imgui.ini): the
        // per-user preference directory rather than beside the
        // executable, matching the shader cache's use of the same pref
        // path. ImGui stores io.IniFilename as a raw pointer rather than
        // copying it, so the backing string must outlive the context;
        // file-scope storage does.
        std::string g_ini_path;
    } // namespace

    void init()
    {
        if (g_backend != backend_mode::none)
        {
            return;
        }

        auto& eng = runtime::current_engine();
        const core::graphics_backend backend = eng.settings->graphics.backend;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        g_ini_path =
            core::platform::path_to_utf8(core::platform::pref_path("AlphaEngine", "AlphaEngine") / "imgui.ini");
        io.IniFilename = g_ini_path.c_str();
        ImGui::StyleColorsDark();

        bool ok = false;
        if (backend == core::graphics_backend::vulkan)
        {
            ok = init_vulkan(eng);
        }
        else
        {
            ok = init_opengl(eng);
        }

        if (!ok)
        {
            ImGui::DestroyContext();
            g_backend = backend_mode::none;
            return;
        }

        LOG_INF("editor: ImGui overlay initialised (SDL3 + %s)", core::graphics_backend_name(backend));
    }

    void shutdown()
    {
        if (g_backend == backend_mode::none)
        {
            return;
        }
        if (g_backend == backend_mode::vulkan)
        {
            // The render queue must be idle before tearing the backend's
            // GPU resources down, and every descriptor-set release the
            // render-target viewer deferred while the run was live (see
            // defer_release_vulkan_texture_binding) must have actually
            // run by now too, or its captured VkDescriptorSet dangles
            // once ImGui_ImplVulkan_Shutdown reclaims the pool it came
            // from.
            auto* device = static_cast<gpu::backend::vulkan::vk_device*>(runtime::current_engine().gpu.get());
            device->flush_pending_destroys();
            // Release the render-target viewer's remaining descriptor sets
            // before the backend's descriptor pool goes with
            // ImGui_ImplVulkan_Shutdown.
            clear_vulkan_texture_bindings();
            ImGui_ImplVulkan_Shutdown();
        }
        else
        {
            ImGui_ImplOpenGL3_Shutdown();
        }
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        g_backend = backend_mode::none;
        g_frame_ready = false;
        g_vulkan_render_pass = VK_NULL_HANDLE;
        g_selected_node = nullptr;
        LOG_INF("editor: ImGui overlay shut down");
    }

    void process_event(const void* sdl_event)
    {
        if (g_backend == backend_mode::none || sdl_event == nullptr)
        {
            return;
        }
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdl_event));
    }

    void begin_frame()
    {
        if (g_backend == backend_mode::none)
        {
            return;
        }

        if (g_backend == backend_mode::vulkan)
        {
            ImGui_ImplVulkan_NewFrame();
        }
        else
        {
            ImGui_ImplOpenGL3_NewFrame();
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        build_panels();

        ImGui::Render();
        g_frame_ready = true;
    }

    void record_draw_data(gpu::render_pass_encoder& encoder)
    {
        if (g_backend == backend_mode::none || !g_frame_ready)
        {
            return;
        }
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data == nullptr)
        {
            return;
        }

        if (g_backend == backend_mode::opengl)
        {
            // The debug pass left the swapchain framebuffer bound;
            // the immediate-mode GL backend draws straight into it.
            ImGui_ImplOpenGL3_RenderDrawData(draw_data);
        }
        else if (g_backend == backend_mode::vulkan)
        {
            // Null while the debug pass is not open — no swapchain
            // image this frame (minimised) — so nothing is recorded
            // outside a render pass.
            auto* cmd = static_cast<VkCommandBuffer>(encoder.native_command_buffer());
            if (cmd != VK_NULL_HANDLE && refresh_vulkan_pipeline(encoder))
            {
                ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
            }
        }
        g_frame_ready = false;
    }

    bool wants_keyboard()
    {
        return g_backend != backend_mode::none && ImGui::GetIO().WantCaptureKeyboard;
    }

    bool wants_mouse()
    {
        return g_backend != backend_mode::none && ImGui::GetIO().WantCaptureMouse;
    }
} // namespace rendering_engine::editor

#else // ALPHAENGINE_HAS_IMGUI

// Release builds (and any configuration without ImGui) get inert stubs so
// the always-compiled engine core can call the layer unconditionally.
namespace rendering_engine::editor
{
    void init() {}
    void shutdown() {}
    void process_event(const void*) {}
    void begin_frame() {}
    void record_draw_data(gpu::render_pass_encoder&) {}

    bool wants_keyboard()
    {
        return false;
    }

    bool wants_mouse()
    {
        return false;
    }
} // namespace rendering_engine::editor

#endif // ALPHAENGINE_HAS_IMGUI
