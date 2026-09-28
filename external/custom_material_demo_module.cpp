// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <assets/color.hpp>
#include <assets/image.hpp>
#include <assets/mesh_generators.hpp>
#include <assets/vertex.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/material_library.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>

#include <cmath>
#include <memory>
#include <utility>

// A material type declared entirely by this module: a rim-light shader
// shipped as two asset files under content/shaders/demo/ and read from the
// content directory in every build type, its vertex format, parameter
// block, texture slot, keyword and default render state. Three spheres
// draw with three instances of it, each with parameters of its own: a
// plain one, one sampling a procedural checker map whose uv scrolls, and a
// translucent one whose rim this module pulses every frame.

namespace
{
    namespace math = core::math;
    using rendering_engine::material_parameter_type;

    // The rim-light type: the shaders by their shader-library names, the
    // record they read (a position+uv+normal prefix, which every generated
    // shape's record starts with), the std140 parameter block the fragment
    // shader declares, the albedo slot with the engine keyword that turns
    // its sampling on, and a keyword of the type's own.
    rendering_engine::material_template_descriptor rim_light_type()
    {
        rendering_engine::material_template_descriptor type{};
        type.name = "rim_light";
        type.vertex_shader.path = "demo/rim_light.vert.glsl";
        type.fragment_shader.path = "demo/rim_light.frag.glsl";
        type.required_vertex_format = assets::vertex_format::position_uv_normal;
        type.parameters = {
            {"base_color", material_parameter_type::float4, 0, math::vec4{0.08f, 0.10f, 0.14f, 1.0f}},
            {"rim_color", material_parameter_type::float4, 16, math::vec4{0.30f, 0.80f, 1.00f, 1.0f}},
            {"rim_power", material_parameter_type::float1, 32, math::vec4{3.0f}},
            {"rim_strength", material_parameter_type::float1, 36, math::vec4{1.5f}},
            {"uv_scroll", material_parameter_type::float2, 40, math::vec4{0.0f, 0.25f, 0.0f, 0.0f}},
        };
        type.textures = {{"albedo",
                          rendering_engine::gpu::shader_bindings::material_albedo_map,
                          rendering_engine::gpu::texture_dimension::d2,
                          "USE_ALBEDO_MAP"}};
        type.keywords = {"ANIMATED_UV"};
        return type;
    }

    // A two-tone checker for the textured sphere, built in memory rather
    // than read from a file.
    assets::image checker_image()
    {
        constexpr uint32_t size = 64;
        constexpr uint32_t cell = 8;
        assets::image image{size, size, assets::color{255, 255, 255, 255}};
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                if (((x / cell) + (y / cell)) % 2 == 1)
                {
                    image.set_pixel(x, y, assets::color{40, 60, 90, 255});
                }
            }
        }
        return image;
    }

    // Pulses the rim of the material it is handed, every frame.
    struct rim_pulse final : runtime::behavior
    {
        explicit rim_pulse(std::shared_ptr<rendering_engine::material> material) : m_material{std::move(material)} {}

        void on_update(float delta_time) override
        {
            m_seconds += delta_time / 1000.0f;
            m_material->set_float("rim_strength", 1.75f + 1.25f * std::sin(m_seconds * 2.0f));
        }

    private:
        std::shared_ptr<rendering_engine::material> m_material;
        float m_seconds{0.0f};
    };

    // A sphere drawn with @p material on a new child of @p parent at
    // @p position; its mesh component shares ownership of the material.
    runtime::node& spawn_sphere(runtime::scene& scene,
                                runtime::node& parent,
                                std::shared_ptr<rendering_engine::material> material,
                                const math::vec3& position)
    {
        std::shared_ptr<rendering_engine::mesh_asset> mesh =
            runtime::current_engine().assets->get_or_create_mesh(assets::mesh_generators::sphere{});
        runtime::node& sphere = scene.create_node({}, &parent);
        sphere.transform.set_position(position);
        sphere.add_component(runtime::mesh_component{std::move(material), std::move(mesh)});
        return sphere;
    }
} // namespace

GAME_MODULE()
{
    runtime::engine& engine = runtime::current_engine();
    rendering_engine::material_library& library = engine.renderer->materials();

    // The type is registered once per process; a later bootstrap finds it.
    std::shared_ptr<rendering_engine::material_template> rim_light = library.find_template("rim_light");
    if (rim_light == nullptr)
    {
        rim_light = library.create_template(rim_light_type());
    }
    if (rim_light == nullptr)
    {
        // create_template logged why the descriptor was refused.
        return;
    }

    // The camera looks from -X toward the origin; the spheres stand side by
    // side across it, each with an instance of its own.
    runtime::node& demo = scene.create_node("custom_material_demo");

    std::shared_ptr<rendering_engine::material> plain = library.create_material(rim_light);
    spawn_sphere(scene, demo, plain, math::vec3{0.0f, 2.4f, 0.0f});

    std::shared_ptr<rendering_engine::material> textured = library.create_material(rim_light);
    textured->set_float4("base_color", math::vec4{1.0f, 0.85f, 0.6f, 1.0f});
    textured->set_float4("rim_color", math::vec4{1.0f, 0.45f, 0.1f, 1.0f});
    textured->set_texture(
        "albedo", engine.assets->load_texture_from_image("procedural:custom_material_demo/checker", checker_image()));
    textured->set_keyword_enabled("ANIMATED_UV", true);
    spawn_sphere(scene, demo, textured, math::vec3{0.0f, 0.0f, 0.0f});

    std::shared_ptr<rendering_engine::material> glass = library.create_material(rim_light);
    glass->set_float4("base_color", math::vec4{0.5f, 0.2f, 0.6f, 0.15f});
    glass->set_float4("rim_color", math::vec4{0.95f, 0.35f, 1.0f, 1.0f});
    glass->set_float("rim_power", 2.0f);
    glass->set_transparent(true);
    runtime::node& glass_sphere = spawn_sphere(scene, demo, glass, math::vec3{0.0f, -2.4f, 0.0f});
    runtime::add_behavior<rim_pulse>(glass_sphere, glass);
}
