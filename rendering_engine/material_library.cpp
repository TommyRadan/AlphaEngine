// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/material_library.hpp>

#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/lighting/environment_probe.hpp>
#include <rendering_engine/materials/basic_material.hpp>
#include <rendering_engine/materials/grid_material.hpp>
#include <rendering_engine/materials/instanced_material.hpp>
#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/materials/points_material.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/materials/ui_material.hpp>

rendering_engine::material_library::material_library() = default;
rendering_engine::material_library::~material_library() = default;

void rendering_engine::material_library::init(gpu::device& device,
                                              gpu::bind_group_layout scene_frame_layout,
                                              gpu::bind_group_layout ui_frame_layout)
{
    m_device = &device;
    m_scene_frame_layout = scene_frame_layout;
    m_ui_frame_layout = ui_frame_layout;

    // One template per built-in type (shaders, layouts, the pipeline-variant
    // cache), registered through create_template exactly as a game's type
    // is, and the shared built-in instance of each. The 3D templates
    // reserve slot 0 for the scene_pass's per-frame group; the ui template
    // reserves it for the ui_pass's. The standard template is also held
    // here so create_standard_material hands every extra instance the same
    // one, and its instances start lit by the current environment.
    m_basic_material = std::make_unique<basic_material>(create_template(basic_material::describe()));
    m_instanced_material = std::make_unique<instanced_material>(create_template(instanced_material::describe()));
    m_phong_material = std::make_unique<phong_material>(create_template(phong_material::describe()));
    material_template_descriptor standard = standard_material::describe();
    standard.instance_factory = [this](std::shared_ptr<material_template> tmpl) -> std::unique_ptr<material>
    {
        auto instance = std::make_unique<standard_material>(std::move(tmpl));
        if (m_environment != nullptr)
        {
            instance->set_environment(*m_environment);
        }
        return instance;
    };
    m_standard_template = create_template(std::move(standard));
    m_standard_material = std::make_unique<standard_material>(m_standard_template);
    m_points_material = std::make_unique<points_material>(create_template(points_material::describe()));
    // The scene lines and the depth-disabled debug-gizmo lines (which
    // always read on top in the depth-less debug pass) are two instances
    // of one line template, bound to two pipeline variants.
    const std::shared_ptr<material_template> line_template = create_template(line_material::describe());
    m_line_material = std::make_unique<line_material>(line_template);
    m_debug_line_material = std::make_unique<line_material>(line_template, /*depth_tested=*/false);
    // Analytic infinite-grid material at the default fade distance.
    m_grid_material = std::make_unique<grid_material>(create_template(grid_material::describe()));
    m_ui_material = std::make_unique<ui_material>(create_template(ui_material::describe()));
    LOG_INF("Rendering Engine: %zu material templates registered (basic, instanced, phong, standard, points, line, "
            "grid, ui)",
            m_templates.size());
}

void rendering_engine::material_library::quit()
{
    // The instances own pipelines that reference the device; release them
    // before the device tears its pools down.
    m_ui_material.reset();
    m_grid_material.reset();
    m_debug_line_material.reset();
    m_line_material.reset();
    m_points_material.reset();
    m_standard_material.reset();
    m_phong_material.reset();
    m_instanced_material.reset();
    m_basic_material.reset();
    // Then every template, built-in and registered by a game alike, while
    // the device is still up.
    m_standard_template.reset();
    m_templates.clear();
    m_environment = nullptr;
    m_device = nullptr;
    m_scene_frame_layout = {};
    m_ui_frame_layout = {};
}

rendering_engine::gpu::bind_group_layout rendering_engine::material_library::frame_layout(material_frame frame) const
{
    switch (frame)
    {
    case material_frame::scene:
        return m_scene_frame_layout;
    case material_frame::ui:
        return m_ui_frame_layout;
    case material_frame::none:
        break;
    }
    return {};
}

std::shared_ptr<rendering_engine::material_template>
rendering_engine::material_library::build_template(material_template_descriptor descriptor) const
{
    assert(m_device != nullptr && "material_library: templates are built between init and quit");
    const gpu::bind_group_layout layout = frame_layout(descriptor.frame);
    return std::make_shared<material_template>(*m_device, std::move(descriptor), layout);
}

std::shared_ptr<rendering_engine::material_template>
rendering_engine::material_library::create_template(material_template_descriptor descriptor)
{
    if (const std::string problem = material_template::validate(descriptor); !problem.empty())
    {
        LOG_ERR("Material template '%s' not created: %s", descriptor.name.c_str(), problem.c_str());
        return nullptr;
    }
    if (find_template(descriptor.name) != nullptr)
    {
        LOG_ERR("Material template '%s' not created: a template of that name is registered already",
                descriptor.name.c_str());
        return nullptr;
    }
    std::shared_ptr<material_template> made = build_template(std::move(descriptor));
    LOG_DBG("Material template '%s' registered (%s, %s)",
            made->name().c_str(),
            made->descriptor().vertex_shader.path.c_str(),
            made->descriptor().fragment_shader.path.c_str());
    m_templates.push_back(made);
    return made;
}

std::shared_ptr<rendering_engine::material_template>
rendering_engine::material_library::find_template(std::string_view name) const
{
    for (const std::shared_ptr<material_template>& registered : m_templates)
    {
        if (registered->name() == name)
        {
            return registered;
        }
    }
    return nullptr;
}

std::unique_ptr<rendering_engine::material>
rendering_engine::material_library::create_material(const std::shared_ptr<material_template>& tmpl) const
{
    if (tmpl == nullptr)
    {
        return nullptr;
    }
    if (const material_instance_factory& factory = tmpl->descriptor().instance_factory; factory)
    {
        return factory(tmpl);
    }
    return std::make_unique<material>(tmpl);
}

void rendering_engine::material_library::report_create_failure(std::string_view name, bool wrong_type)
{
    LOG_ERR("material_library::create_material: %s '%.*s'",
            wrong_type ? "the requested type is not what the template makes for" : "no template registered as",
            static_cast<int>(name.size()),
            name.data());
}

rendering_engine::basic_material& rendering_engine::material_library::get_basic_material()
{
    return *m_basic_material;
}

rendering_engine::instanced_material& rendering_engine::material_library::get_instanced_material()
{
    return *m_instanced_material;
}

rendering_engine::phong_material& rendering_engine::material_library::get_phong_material()
{
    return *m_phong_material;
}

rendering_engine::standard_material& rendering_engine::material_library::get_standard_material()
{
    return *m_standard_material;
}

std::unique_ptr<rendering_engine::standard_material>
rendering_engine::material_library::create_standard_material(const environment_probe* environment)
{
    auto material = std::make_unique<standard_material>(m_standard_template);
    if (environment != nullptr)
    {
        material->set_environment(*environment);
    }
    return material;
}

const std::shared_ptr<rendering_engine::material_template>&
rendering_engine::material_library::get_standard_material_template() const
{
    return m_standard_template;
}

void rendering_engine::material_library::set_environment(const environment_probe* environment)
{
    // Mirror the choice onto every live standard material — the built-in
    // one and each instance create_standard_material or create_material
    // handed out, whenever it was made — so all their surfaces pick up the
    // matching image-based ambient, and remember it for the instances made
    // later. Every instance of the standard template is a
    // standard_material: that is the only type constructed over it.
    m_environment = environment;
    if (m_standard_template == nullptr)
    {
        return;
    }
    // Copy the list: set_environment rebuilds the instance's bind group
    // but never registers or drops an instance, so this is only caution.
    const std::vector<material*> instances = m_standard_template->instances();
    for (material* instance : instances)
    {
        auto* standard = static_cast<standard_material*>(instance);
        if (environment != nullptr)
        {
            standard->set_environment(*environment);
        }
        else
        {
            standard->clear_environment();
        }
    }
}

void rendering_engine::material_library::refresh_texture_assets()
{
    // Only instances that sample shared texture assets rebuild anything:
    // the standard materials' maps and the declared texture slots of a
    // game's templates.
    for (const std::shared_ptr<material_template>& registered : m_templates)
    {
        for (material* instance : registered->instances())
        {
            instance->refresh_texture_assets();
        }
    }
}

rendering_engine::points_material& rendering_engine::material_library::get_points_material()
{
    return *m_points_material;
}

rendering_engine::line_material& rendering_engine::material_library::get_line_material()
{
    return *m_line_material;
}

rendering_engine::line_material& rendering_engine::material_library::get_debug_line_material()
{
    return *m_debug_line_material;
}

rendering_engine::grid_material& rendering_engine::material_library::get_grid_material()
{
    return *m_grid_material;
}

std::unique_ptr<rendering_engine::grid_material>
rendering_engine::material_library::create_grid_material(float fade_distance)
{
    // The fade distance is a define baked into the template's shaders, so
    // the new instance gets a template of its own, built like the
    // registered grid template but left out of the registry: it goes with
    // its instance.
    assert(m_grid_material != nullptr && "material_library::create_grid_material is only valid between init and quit");
    return std::make_unique<grid_material>(build_template(grid_material::describe(fade_distance)));
}

rendering_engine::ui_material& rendering_engine::material_library::get_ui_material()
{
    return *m_ui_material;
}
