// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/material_library.hpp>

#include <cassert>
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
    // One template per type (shaders, layouts, the pipeline-variant cache)
    // against the per-frame layouts exposed by the passes, and the built-in
    // instance of each. The 3D templates reserve slot 0 for the scene_pass's
    // per-frame group; the ui template reserves it for the ui_pass's. Each
    // instance keeps its template alive; the standard template is also held
    // here so create_standard_material hands every extra instance the same
    // one.
    m_basic_material = std::make_unique<basic_material>(basic_material::create_template(device, scene_frame_layout));
    m_instanced_material =
        std::make_unique<instanced_material>(instanced_material::create_template(device, scene_frame_layout));
    m_phong_material = std::make_unique<phong_material>(phong_material::create_template(device, scene_frame_layout));
    m_standard_template = standard_material::create_template(device, scene_frame_layout);
    m_standard_material = std::make_unique<standard_material>(m_standard_template);
    m_points_material = std::make_unique<points_material>(points_material::create_template(device, scene_frame_layout));
    // The scene lines and the depth-disabled debug lines (which always
    // read on top in the depth-less debug pass) are two instances of one
    // line template, bound to two pipeline variants; the template is held
    // here too so create_line_material hands further instances the same
    // one.
    m_line_template = line_material::create_template(device, scene_frame_layout);
    m_line_material = std::make_unique<line_material>(m_line_template);
    m_debug_line_material = std::make_unique<line_material>(m_line_template, /*depth_tested=*/false);
    // Analytic infinite-grid material; shares the scene per-frame layout.
    m_grid_material = std::make_unique<grid_material>(grid_material::create_template(device, scene_frame_layout));
    m_ui_material = std::make_unique<ui_material>(ui_material::create_template(device, ui_frame_layout));
    LOG_INF("Rendering Engine: basic_material, instanced_material, phong_material, standard_material, points_material, "
            "line_material and ui_material constructed");
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
    // The other templates went with their last instance above; the
    // standard and line ones are held here too and must go before the
    // device.
    m_line_template.reset();
    m_standard_template.reset();
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
    // one and each instance create_standard_material handed out, whenever
    // it was made — so all their surfaces pick up the matching image-based
    // ambient. Every instance of the standard template is a
    // standard_material: that is the only type constructed over it.
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
    // Every instance of the standard template is a standard_material.
    if (m_standard_template == nullptr)
    {
        return;
    }
    for (material* instance : m_standard_template->instances())
    {
        static_cast<standard_material*>(instance)->refresh_texture_assets();
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

std::unique_ptr<rendering_engine::line_material>
rendering_engine::material_library::create_line_material(bool depth_tested)
{
    assert(m_line_template != nullptr && "material_library::create_line_material is only valid between init and quit");
    return std::make_unique<line_material>(m_line_template, depth_tested);
}

rendering_engine::grid_material& rendering_engine::material_library::get_grid_material()
{
    return *m_grid_material;
}

std::unique_ptr<rendering_engine::grid_material>
rendering_engine::material_library::create_grid_material(float fade_distance)
{
    // The fade distance is a define baked into the template's shaders, so
    // the new instance gets a template of its own, built on the device and
    // against the scene per-frame layout the built-in grid template uses.
    assert(m_grid_material != nullptr && "material_library::create_grid_material is only valid between init and quit");
    const material_template& builtin = m_grid_material->get_template();
    return std::make_unique<grid_material>(
        grid_material::create_template(builtin.device(), builtin.descriptor().frame_layout, fade_distance));
}

rendering_engine::ui_material& rendering_engine::material_library::get_ui_material()
{
    return *m_ui_material;
}
