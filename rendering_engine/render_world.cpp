// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/render_world.hpp>

#include <algorithm>

#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/lighting/environment_probe.hpp>
#include <rendering_engine/lighting/light.hpp>

void rendering_engine::render_world::quit()
{
    rendering_engine::set_drawable_aspect(0.0f);
}

void rendering_engine::render_world::register_scene_renderable(renderable* r)
{
    if (r != nullptr)
    {
        m_scene_renderables.push_back(r);
    }
}

void rendering_engine::render_world::unregister_scene_renderable(renderable* r)
{
    m_scene_renderables.erase(std::remove(m_scene_renderables.begin(), m_scene_renderables.end(), r),
                              m_scene_renderables.end());
}

void rendering_engine::render_world::register_ui_renderable(renderable* r)
{
    if (r != nullptr)
    {
        m_ui_renderables.push_back(r);
    }
}

void rendering_engine::render_world::unregister_ui_renderable(renderable* r)
{
    m_ui_renderables.erase(std::remove(m_ui_renderables.begin(), m_ui_renderables.end(), r), m_ui_renderables.end());
}

void rendering_engine::render_world::register_debug_renderable(renderable* r)
{
    if (r != nullptr)
    {
        m_debug_renderables.push_back(r);
    }
}

void rendering_engine::render_world::unregister_debug_renderable(renderable* r)
{
    m_debug_renderables.erase(std::remove(m_debug_renderables.begin(), m_debug_renderables.end(), r),
                              m_debug_renderables.end());
}

const std::vector<rendering_engine::light*>& rendering_engine::render_world::lights() const
{
    return registered_lights();
}

const std::vector<rendering_engine::camera*>& rendering_engine::render_world::cameras() const
{
    return registered_cameras();
}

rendering_engine::camera* rendering_engine::render_world::active_camera() const
{
    return rendering_engine::active_camera();
}

void rendering_engine::render_world::set_drawable_aspect(float aspect_ratio)
{
    rendering_engine::set_drawable_aspect(aspect_ratio);
}

void rendering_engine::render_world::set_environment(const environment_probe* probe)
{
    m_environment = probe;
}

rendering_engine::gpu::texture rendering_engine::render_world::environment_brdf_lut() const
{
    return m_environment != nullptr ? m_environment->brdf_lut() : gpu::texture{};
}

void rendering_engine::render_world::set_fog(const fog_settings& fog)
{
    m_fog = fog;
}
