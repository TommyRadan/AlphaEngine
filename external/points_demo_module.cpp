// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <assets/color.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/points_material.hpp>
#include <rendering_engine/renderables/points.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/engine.hpp>

namespace
{
    namespace math = core::math;

    // Number of points on the Fibonacci-sphere cloud.
    constexpr int point_count = 2000;

    // Turns its node about the world up axis (+Z) at a constant rate.
    struct turntable final : runtime::behavior
    {
        void on_update(float delta_time) override
        {
            m_angle += rotation_speed * (delta_time / 1000.0f);
            owner().transform.set_rotation(math::vec3{0.0f, 0.0f, m_angle});
        }

    private:
        static constexpr float rotation_speed = 3.14f / 6; // radians / second

        float m_angle{0.0f};
    };
} // namespace

GAME_MODULE()
{
    auto& material = runtime::current_engine().renderer->get_points_material();
    material.set_size(6.0f);
    material.set_size_attenuation(true);
    material.set_color(assets::color{255, 255, 255, 255});

    // Scatter points evenly over a unit sphere with the Fibonacci
    // spiral, colouring each by its position so the cloud reads in 3D.
    std::vector<math::vec3> positions;
    std::vector<math::vec3> colors;
    positions.reserve(point_count);
    colors.reserve(point_count);

    const float golden_angle = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < point_count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(point_count - 1);
        const float z = 1.0f - 2.0f * t;
        const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float theta = golden_angle * static_cast<float>(i);
        const math::vec3 position{radius * std::cos(theta), radius * std::sin(theta), z};
        positions.push_back(position);
        colors.push_back(math::vec3{0.5f + 0.5f * position.x, 0.5f + 0.5f * position.y, 0.5f + 0.5f * position.z});
    }

    auto dots = std::make_unique<rendering_engine::points>(*runtime::current_engine().gpu, &material);
    dots->set_positions(positions, colors);
    dots->upload();

    // The cloud turns with its node.
    runtime::node& cloud = scene.create_node("point_cloud");
    cloud.add_component(runtime::renderable_component{std::move(dots)});
    runtime::add_behavior<turntable>(cloud);
}
