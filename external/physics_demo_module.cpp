// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <core/math/math.hpp>
#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/renderables/premade_3d/box.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/components/collider_component.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/components/rigidbody_component.hpp>
#include <runtime/engine.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

// Physics showcase, sized to sit beside the shadow demo: a
// few boxes dropped onto a pedestal that stands on the shadow demo's
// ground plane, just left of the camera's starting line of sight. Nothing
// here says what shape anything is — every collider is fitted to the
// premade box its node draws, the pedestal (a collider alone) is a static
// body and each crate (a rigidbody alone) a dynamic one. The crates are
// dropped again every few seconds. Debug builds draw the colliders and
// contacts on top (the overlay's Helpers panel toggles "Physics").
//
//   physics_demo    (box_drop: the materials; re-drops the crates)
//   ├── pedestal    (box + collider)
//   └── 5 crates    (box + rigidbody)
namespace
{
    namespace math = core::math;

    // The pedestal stands on the shadow demo's ground (world Z -1.5).
    constexpr float ground_z = -1.5f;
    constexpr float pedestal_height = 0.3f;
    constexpr math::vec3 pedestal_center{2.5f, 2.0f, ground_z + pedestal_height * 0.5f};

    constexpr float crate_size = 0.5f;
    constexpr float drop_period = 8.0f; // seconds between drops

    // Where each crate starts a drop, relative to the pedestal's centre, and
    // how it is tumbled (Euler radians).
    constexpr std::array<math::vec3, 5> drop_offsets{math::vec3{0.0f, 0.0f, 1.0f},
                                                     math::vec3{0.2f, -0.1f, 1.8f},
                                                     math::vec3{-0.1f, 0.25f, 2.6f},
                                                     math::vec3{0.15f, 0.1f, 3.4f},
                                                     math::vec3{-0.2f, -0.2f, 4.2f}};
    constexpr std::array<math::vec3, 5> drop_tumbles{math::vec3{0.0f, 0.0f, 0.2f},
                                                     math::vec3{0.4f, 0.1f, 0.0f},
                                                     math::vec3{0.0f, 0.5f, 0.7f},
                                                     math::vec3{0.8f, 0.0f, 0.3f},
                                                     math::vec3{0.3f, 0.6f, 0.0f}};

    // Owns the materials and drops the crates, now and every drop_period.
    struct box_drop final : runtime::behavior
    {
        rendering_engine::standard_material* make_material(const rendering_engine::color& base)
        {
            auto material = runtime::current_engine().renderer->create_standard_material();
            material->set_base_color(base);
            material->set_metalness(0.0f);
            material->set_roughness(0.6f);
            m_materials.push_back(std::move(material));
            return m_materials.back().get();
        }

        void add_crate(runtime::node& crate)
        {
            m_crates.push_back(&crate);
        }

        void drop()
        {
            for (std::size_t i = 0; i < m_crates.size(); ++i)
            {
                runtime::node& crate = *m_crates[i];
                // Moving a dynamic body's node teleports the body.
                crate.transform.set_position(pedestal_center + drop_offsets[i % drop_offsets.size()]);
                crate.transform.set_rotation(drop_tumbles[i % drop_tumbles.size()]);
                if (auto* body = crate.get_component<runtime::rigidbody_component>())
                {
                    body->set_linear_velocity(math::vec3{});
                    body->set_angular_velocity(math::vec3{});
                }
            }
        }

        void on_fixed_update(float delta_time) override
        {
            m_elapsed += delta_time / 1000.0f;
            if (m_elapsed >= drop_period)
            {
                m_elapsed = 0.0f;
                drop();
            }
        }

    private:
        std::vector<std::unique_ptr<rendering_engine::standard_material>> m_materials;
        // Children of this behaviour's node: the scene frees them only when
        // it tears the demo down, after the last fixed update.
        std::vector<runtime::node*> m_crates;
        float m_elapsed{0.0f};
    };
} // namespace

GAME_MODULE()
{
    runtime::node& demo = scene.create_node("physics_demo");
    box_drop* dropper = runtime::add_behavior<box_drop>(demo);
    if (dropper == nullptr)
    {
        return;
    }

    auto pedestal_box = std::make_unique<rendering_engine::box>(
        dropper->make_material(rendering_engine::color{150, 150, 160, 255}), 3.0f, 3.0f, pedestal_height);
    pedestal_box->upload();
    runtime::node& pedestal = scene.create_node("pedestal", &demo);
    pedestal.transform.set_position(pedestal_center);
    pedestal.add_component(runtime::renderable_component{std::move(pedestal_box)});
    pedestal.add_component(runtime::collider_component{});

    rendering_engine::standard_material* crate_material =
        dropper->make_material(rendering_engine::color{230, 126, 34, 255});
    for (std::size_t i = 0; i < drop_offsets.size(); ++i)
    {
        auto crate_box = std::make_unique<rendering_engine::box>(crate_material, crate_size, crate_size, crate_size);
        crate_box->upload();
        runtime::node& crate = scene.create_node("crate", &demo);
        crate.add_component(runtime::renderable_component{std::move(crate_box)});
        crate.add_component(runtime::rigidbody_component{runtime::physics::body_type::dynamic_body, 2.0f});
        dropper->add_crate(crate);
    }
    dropper->drop();
}
