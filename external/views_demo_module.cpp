// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file views_demo_module.cpp
 * @brief Showcase for views: a camera rendering into a texture that a
 *        monitor in the scene shows, and split screen.
 *
 * A few coloured shapes stand on a ground plane under a shadow-casting sun,
 * a torus spinning in their middle. A security camera on a mast looks at
 * them from the side and renders its view into a render texture, which a
 * monitor standing next to the shapes shows as its emissive screen, so the
 * player's camera (camera_module) sees the scene twice: directly and on
 * the monitor. The screen is on a layer of its own that the security
 * camera leaves out, so its view never samples the texture it renders
 * into.
 *
 * Every few seconds the showcase switches between one view of the whole
 * window and split screen: the player's camera takes the left half and an
 * overview camera the right half, each with its own targets, temporal
 * history and aspect. The render texture keeps its size however the window
 * is resized; the views on the window follow it.
 *
 * Everything lives in a scene of its own, which the engine unloads on
 * shutdown: the shapes, the cameras and the monitor first, then the
 * showcase that owns the render texture and the materials they draw with.
 */

#include "api/game_module.hpp"

#include <assets/color.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/render_texture.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/renderables/premade_3d/box.hpp>
#include <rendering_engine/renderables/premade_3d/plane.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/renderables/premade_3d/torus.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/scene_manager.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace
{
    namespace math = core::math;

    // The monitor's screen is on this layer, which the security camera
    // leaves out of its culling mask.
    constexpr uint32_t layer_monitor = 1u << 1;

    // The security camera's texture: fixed, whatever the window's size.
    constexpr uint32_t screen_width = 512;
    constexpr uint32_t screen_height = 288;

    // Seconds between the switches from one view to split screen and back.
    constexpr float mode_seconds = 3.0f;

    const rendering_engine::viewport_rect whole_window{0.0f, 0.0f, 1.0f, 1.0f};
    const rendering_engine::viewport_rect left_half{0.0f, 0.0f, 0.5f, 1.0f};
    const rendering_engine::viewport_rect right_half{0.5f, 0.0f, 0.5f, 1.0f};

    // The camera component whose proxy is @p camera, in whichever loaded
    // scene it lives, or null.
    runtime::camera_component* find_camera(rendering_engine::camera_proxy_handle camera)
    {
        runtime::camera_component* found = nullptr;
        runtime::scene_manager& scenes = *runtime::current_engine().scenes;
        for (std::size_t index = 0; index < scenes.scene_count() && found == nullptr; ++index)
        {
            scenes.scene_at(index).each<runtime::camera_component>(
                [&found, camera](runtime::node&, runtime::camera_component& component)
                {
                    if (component.proxy() == camera)
                    {
                        found = &component;
                    }
                });
        }
        return found;
    }

    // A perspective camera for a rectangle of @p aspect; the camera
    // component keeps the aspect matched to the rectangle it renders into.
    std::unique_ptr<rendering_engine::perspective_camera> make_camera(float aspect)
    {
        return std::make_unique<rendering_engine::perspective_camera>(60.0f, aspect, 0.1f, 200.0f);
    }

    // Turns its node about the world up axis.
    struct spinner final : runtime::behavior
    {
        void on_update(float delta_time) override
        {
            m_angle += 0.8f * delta_time / 1000.0f;
            owner().transform.set_rotation(math::vec3{0.6f, 0.0f, m_angle});
        }

    private:
        float m_angle{0.0f};
    };

    // The showcase's root: owns the render texture and the materials, and
    // switches between one view and split screen. The nodes that draw with
    // them are its descendants, so they are freed first.
    struct views_showcase final : runtime::behavior
    {
        views_showcase()
            : m_screen{std::make_unique<rendering_engine::render_texture>(
                  *runtime::current_engine().gpu, screen_width, screen_height)}
        {
        }

        const rendering_engine::render_texture& screen() const noexcept
        {
            return *m_screen;
        }

        rendering_engine::standard_material* make_material(const assets::color& base, float roughness)
        {
            auto material = runtime::current_engine().renderer->create_standard_material();
            material->set_base_color(base);
            material->set_metalness(0.0f);
            material->set_roughness(roughness);
            m_materials.push_back(std::move(material));
            return m_materials.back().get();
        }

        // The overview camera that takes the right half in split screen.
        void set_overview(runtime::node& overview)
        {
            m_overview = &overview;
        }

        void on_update(float delta_time) override
        {
            // The player's camera comes from another module; it is the one
            // on the window while the overview is off.
            if (!m_player.valid())
            {
                runtime::scene* scene = owner().scene();
                const rendering_engine::render_world* world = scene != nullptr ? scene->world() : nullptr;
                m_player = world != nullptr ? world->active_camera() : rendering_engine::camera_proxy_handle{};
                if (!m_player.valid())
                {
                    return;
                }
            }

            m_time += delta_time / 1000.0f;
            const bool split = static_cast<int>(m_time / mode_seconds) % 2 == 1;
            if (split != m_split)
            {
                apply(split);
            }
        }

        void on_disable() override
        {
            if (m_split)
            {
                apply(false);
            }
        }

    private:
        // Split screen: the player's camera on the left half, the overview
        // camera enabled on the right; otherwise the player's camera on the
        // whole window and the overview off, so its view and history go.
        void apply(bool split)
        {
            m_split = split;
            if (runtime::camera_component* player = find_camera(m_player); player != nullptr)
            {
                player->get()->set_viewport(split ? left_half : whole_window);
            }
            if (m_overview != nullptr)
            {
                if (runtime::camera_component* overview = m_overview->get_component<runtime::camera_component>())
                {
                    overview->get()->set_enabled(split);
                }
            }
            LOG_INF("views_demo: %s", split ? "split screen" : "one view");
        }

        std::unique_ptr<rendering_engine::render_texture> m_screen;
        std::vector<std::unique_ptr<rendering_engine::standard_material>> m_materials;
        runtime::node* m_overview{nullptr};
        rendering_engine::camera_proxy_handle m_player{};
        float m_time{0.0f};
        bool m_split{false};
    };

    // Hangs @p shape on a new child of @p parent at @p position.
    runtime::node& spawn(runtime::scene& scene,
                         runtime::node& parent,
                         const math::vec3& position,
                         std::unique_ptr<rendering_engine::mesh_source> shape)
    {
        runtime::node& prop = scene.create_node({}, &parent);
        prop.transform.set_position(position);
        prop.add_component(runtime::renderable_component{std::move(shape)});
        return prop;
    }
} // namespace

GAME_MODULE()
{
    runtime::scene& demo = runtime::current_engine().scenes->load("views_demo", runtime::load_mode::additive);
    auto& cache = *runtime::current_engine().assets;

    runtime::node& root = demo.create_node("views_demo");
    views_showcase* showcase = runtime::add_behavior<views_showcase>(root);
    if (showcase == nullptr)
    {
        return;
    }

    // The shapes: a ground plane, a ring of spheres and boxes, and a torus
    // spinning in the middle, in front of the player's camera (which looks
    // along +X from x = -5).
    spawn(demo,
          root,
          math::vec3{0.0f, 0.0f, -1.0f},
          std::make_unique<rendering_engine::plane>(
              cache, showcase->make_material(assets::color{150, 150, 145, 255}, 0.9f), 40.0f, 40.0f));
    const assets::color colors[] = {
        {220, 70, 60, 255}, {240, 180, 50, 255}, {80, 190, 90, 255}, {60, 130, 230, 255}, {170, 90, 210, 255}};
    for (int i = 0; i < 5; ++i)
    {
        const float angle = static_cast<float>(i) * 1.2566f;
        const math::vec3 position{2.5f * std::cos(angle), 2.5f * std::sin(angle), -0.5f};
        rendering_engine::standard_material* material = showcase->make_material(colors[i], 0.4f);
        if (i % 2 == 0)
        {
            spawn(demo, root, position, std::make_unique<rendering_engine::sphere>(cache, material, 24, 48))
                .transform.set_scale(math::vec3{0.5f, 0.5f, 0.5f});
        }
        else
        {
            spawn(demo, root, position, std::make_unique<rendering_engine::box>(cache, material));
        }
    }
    runtime::node& torus = spawn(demo,
                                 root,
                                 math::vec3{0.0f, 0.0f, 0.3f},
                                 std::make_unique<rendering_engine::torus>(
                                     cache, showcase->make_material(assets::color{235, 235, 240, 255}, 0.2f)));
    runtime::add_behavior<spinner>(torus);

    // The security camera, on a mast to the side, renders into the texture;
    // a small sphere marks where it stands. It leaves the monitor's layer
    // out, so it never samples its own texture.
    runtime::node& security = demo.create_node("security_camera", &root);
    security.transform.set_position(math::vec3{5.0f, -6.0f, 3.5f});
    security.look_at(math::vec3{0.0f, 0.0f, 0.0f});
    auto security_lens = make_camera(static_cast<float>(screen_width) / static_cast<float>(screen_height));
    security_lens->set_target(&showcase->screen());
    security_lens->set_culling_mask(rendering_engine::layer_all & ~layer_monitor);
    security.add_component(runtime::camera_component{std::move(security_lens)});
    runtime::node& marker = spawn(demo,
                                  root,
                                  math::vec3{5.0f, -6.0f, 3.5f},
                                  std::make_unique<rendering_engine::sphere>(
                                      cache, showcase->make_material(assets::color{40, 40, 45, 255}, 0.5f), 16, 32));
    marker.transform.set_scale(math::vec3{0.25f, 0.25f, 0.25f});

    // The monitor stands to the player's left, facing the player's start.
    // Its screen is a plane (in its XY, facing +Z) whose emissive map is the
    // texture; the texture's row 0 is the bottom of the image, so the plane
    // is flipped along its height to show it upright.
    const math::vec3 monitor_position{3.0f, 3.5f, 0.8f};
    const math::vec3 facing = math::normalize(math::vec3{-8.0f, -3.5f, 0.0f});
    const math::vec3 up = math::world_up;
    runtime::node& monitor = demo.create_node("monitor", &root);
    monitor.transform.set_position(monitor_position);
    monitor.transform.set_quaternion(math::quat_from_basis(math::cross(up, facing), up, facing));
    monitor.transform.set_scale(math::vec3{1.0f, -1.0f, 1.0f});
    rendering_engine::standard_material* screen_material = showcase->make_material(assets::color{0, 0, 0, 255}, 1.0f);
    screen_material->set_emissive(assets::color{255, 255, 255, 255});
    screen_material->set_emissive_intensity(1.0f);
    screen_material->set_emissive_map(showcase->screen().texture());
    auto screen = std::make_unique<rendering_engine::plane>(cache, screen_material, 3.2f, 1.8f);
    screen->set_layer_mask(layer_monitor);
    monitor.add_component(runtime::renderable_component{std::move(screen)});

    // The overview camera, off until split screen puts it on the right half.
    // It ranks below the player's camera, so the player's stays the primary
    // view, whose camera fits the shadows.
    runtime::node& overview = demo.create_node("overview_camera", &root);
    overview.transform.set_position(math::vec3{-2.0f, -9.0f, 6.0f});
    overview.look_at(math::vec3{0.5f, 0.5f, 0.0f});
    auto overview_lens = make_camera(1.0f);
    overview_lens->set_viewport(right_half);
    overview_lens->set_priority(-1);
    overview_lens->set_draws_ui(false);
    overview_lens->set_enabled(false);
    overview.add_component(runtime::camera_component{std::move(overview_lens)});
    showcase->set_overview(overview);

    // A dim fill and a shadow-casting sun.
    auto ambient = std::make_unique<rendering_engine::ambient_light>();
    ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    ambient->intensity = 0.15f;
    demo.create_node("ambient", &root).add_component(runtime::light_component{std::move(ambient)});

    auto sun_light = std::make_unique<rendering_engine::directional_light>();
    sun_light->color = math::vec3{1.0f, 0.96f, 0.9f};
    sun_light->intensity = 2.5f;
    sun_light->cast_shadow = true;
    runtime::node& sun = demo.create_node("sun", &root);
    sun.look_at(sun.world_position() + math::vec3{0.6f, 0.5f, -1.0f});
    sun.add_component(runtime::light_component{std::move(sun_light)});
}
