// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/render_world.hpp>

#include <algorithm>

#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/lighting/environment_probe.hpp>

namespace
{
    // True when @p candidate beats @p incumbent under the arbitration
    // rule: higher priority, else a main tag the incumbent lacks, else
    // (walking the list in attach order) simply being the later entry.
    bool outranks(const rendering_engine::camera& candidate, const rendering_engine::camera* incumbent)
    {
        if (incumbent == nullptr)
        {
            return true;
        }
        if (candidate.get_priority() != incumbent->get_priority())
        {
            return candidate.get_priority() > incumbent->get_priority();
        }
        return candidate.is_main() || !incumbent->is_main();
    }
} // namespace

void rendering_engine::render_world::quit()
{
    set_drawable_aspect(0.0f);
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

void rendering_engine::render_world::add_light(light& l)
{
    m_lights.push_back(&l);
}

void rendering_engine::render_world::remove_light(light& l)
{
    m_lights.erase(std::remove(m_lights.begin(), m_lights.end(), &l), m_lights.end());
}

void rendering_engine::render_world::add_camera(camera& cam)
{
    remove_camera(cam);
    m_cameras.push_back(&cam);
    if (m_drawable_aspect > 0.0f)
    {
        cam.set_aspect_ratio(m_drawable_aspect);
    }
}

void rendering_engine::render_world::remove_camera(camera& cam)
{
    m_cameras.erase(std::remove(m_cameras.begin(), m_cameras.end(), &cam), m_cameras.end());
}

rendering_engine::camera* rendering_engine::render_world::active_camera() const
{
    camera* best = nullptr;
    for (camera* cam : m_cameras)
    {
        if (cam->is_enabled() && outranks(*cam, best))
        {
            best = cam;
        }
    }
    return best;
}

rendering_engine::camera* rendering_engine::render_world::main_camera() const
{
    camera* best = nullptr;
    for (camera* cam : m_cameras)
    {
        if (cam->is_enabled() && cam->is_main() && outranks(*cam, best))
        {
            best = cam;
        }
    }
    return best;
}

void rendering_engine::render_world::set_drawable_aspect(float aspect_ratio)
{
    m_drawable_aspect = aspect_ratio > 0.0f ? aspect_ratio : 0.0f;
    if (aspect_ratio <= 0.0f)
    {
        return;
    }
    for (camera* cam : m_cameras)
    {
        cam->set_aspect_ratio(aspect_ratio);
    }
}

void rendering_engine::render_world::add_helper(debug_draw::helper& h)
{
    m_helpers.push_back(&h);
}

void rendering_engine::render_world::remove_helper(debug_draw::helper& h)
{
    m_helpers.erase(std::remove(m_helpers.begin(), m_helpers.end(), &h), m_helpers.end());
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
