// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <assets/color.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/renderables/line.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/components/renderable_component.hpp>
#include <runtime/engine.hpp>

namespace
{
    namespace math = core::math;

    // Number of samples along the helix strip.
    constexpr int sample_count = 400;

    // Turns its node about the Y axis at a constant rate.
    struct turntable final : runtime::behavior
    {
        void on_update(float delta_time) override
        {
            m_angle += rotation_speed * (delta_time / 1000.0f);
            owner().transform.set_rotation(math::vec3{0.0f, m_angle, 0.0f});
        }

    private:
        static constexpr float rotation_speed = 3.14f / 6; // radians / second

        float m_angle{0.0f};
    };
} // namespace

GAME_MODULE()
{
    auto& material = runtime::current_engine().renderer->get_line_material();
    material.set_color(assets::color{255, 255, 255, 255});

    // Sample a vertical helix and colour each vertex along the way so the
    // strip reads as a continuous rainbow ribbon.
    std::vector<math::vec3> positions;
    std::vector<math::vec3> colors;
    positions.reserve(sample_count);
    colors.reserve(sample_count);

    const float turns = 4.0f;
    for (int i = 0; i < sample_count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sample_count - 1);
        const float angle = turns * 2.0f * 3.14159265f * t;
        const math::vec3 position{std::cos(angle), 2.0f * t - 1.0f, std::sin(angle)};
        positions.push_back(position);
        colors.push_back(math::vec3{0.5f + 0.5f * std::cos(angle), t, 0.5f + 0.5f * std::sin(angle)});
    }

    auto strip = std::make_unique<rendering_engine::line>(*runtime::current_engine().gpu, &material);
    strip->set_mode(rendering_engine::line_mode::strip);
    strip->set_positions(positions, colors);
    strip->upload();

    // The helix turns with its node.
    runtime::node& helix = scene.create_node("helix");
    helix.add_component(runtime::renderable_component{std::move(strip)});
    runtime::add_behavior<turntable>(helix);
}
