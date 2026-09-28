// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/render_world.hpp>

#include <algorithm>
#include <cassert>

#include <rendering_engine/lighting/environment_probe.hpp>

namespace
{
    // True when @p candidate beats @p incumbent under the arbitration
    // rule: higher priority, else a main tag the incumbent lacks, else
    // (walking the proxies in creation order) simply being the later entry.
    bool outranks(const rendering_engine::camera_proxy& candidate, const rendering_engine::camera_proxy* incumbent)
    {
        if (incumbent == nullptr)
        {
            return true;
        }
        if (candidate.priority != incumbent->priority)
        {
            return candidate.priority > incumbent->priority;
        }
        return candidate.main || !incumbent->main;
    }
} // namespace

void rendering_engine::render_world::quit()
{
    set_drawable_aspect(0.0f);
}

void rendering_engine::render_world::begin_frame() noexcept
{
    m_in_frame = true;
}

void rendering_engine::render_world::end_frame() noexcept
{
    m_in_frame = false;
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

rendering_engine::light_proxy_handle rendering_engine::render_world::create_light(const light_proxy& proxy,
                                                                                  bool enabled)
{
    assert(!m_in_frame && "light proxies are created between frames");
    const light_proxy_handle created = m_lights.insert(proxy);
    if (enabled)
    {
        m_enabled_lights.push_back(created);
    }
    return created;
}

void rendering_engine::render_world::destroy_light(light_proxy_handle light)
{
    assert(!m_in_frame && "light proxies are destroyed between frames");
    m_enabled_lights.erase(std::remove(m_enabled_lights.begin(), m_enabled_lights.end(), light),
                           m_enabled_lights.end());
    m_lights.erase(light);
}

void rendering_engine::render_world::set_light_enabled(light_proxy_handle light, bool enabled)
{
    if (!m_lights.contains(light) || is_light_enabled(light) == enabled)
    {
        return;
    }
    if (enabled)
    {
        m_enabled_lights.push_back(light);
    }
    else
    {
        m_enabled_lights.erase(std::remove(m_enabled_lights.begin(), m_enabled_lights.end(), light),
                               m_enabled_lights.end());
    }
}

bool rendering_engine::render_world::is_light_enabled(light_proxy_handle light) const noexcept
{
    return std::find(m_enabled_lights.begin(), m_enabled_lights.end(), light) != m_enabled_lights.end();
}

rendering_engine::light_proxy* rendering_engine::render_world::light(light_proxy_handle light) noexcept
{
    return m_lights.get(light);
}

const rendering_engine::light_proxy* rendering_engine::render_world::light(light_proxy_handle light) const noexcept
{
    return m_lights.get(light);
}

void rendering_engine::render_world::collect_enabled_lights(std::vector<const light_proxy*>& out) const
{
    out.clear();
    out.reserve(m_enabled_lights.size());
    for (const light_proxy_handle handle : m_enabled_lights)
    {
        if (const light_proxy* proxy = m_lights.get(handle))
        {
            out.push_back(proxy);
        }
    }
}

rendering_engine::camera_proxy_handle rendering_engine::render_world::create_camera(const camera_proxy& proxy)
{
    assert(!m_in_frame && "camera proxies are created between frames");
    return m_cameras.insert(proxy);
}

void rendering_engine::render_world::destroy_camera(camera_proxy_handle camera)
{
    assert(!m_in_frame && "camera proxies are destroyed between frames");
    m_cameras.erase(camera);
}

rendering_engine::camera_proxy* rendering_engine::render_world::camera(camera_proxy_handle camera) noexcept
{
    return m_cameras.get(camera);
}

const rendering_engine::camera_proxy* rendering_engine::render_world::camera(camera_proxy_handle camera) const noexcept
{
    return m_cameras.get(camera);
}

rendering_engine::camera_proxy_handle rendering_engine::render_world::active_camera() const
{
    const std::span<const camera_proxy> cameras = m_cameras.values();
    const camera_proxy* best = nullptr;
    camera_proxy_handle winner{};
    for (std::size_t i = 0; i < cameras.size(); ++i)
    {
        if (cameras[i].enabled && outranks(cameras[i], best))
        {
            best = &cameras[i];
            winner = m_cameras.handle_at(i);
        }
    }
    return winner;
}

rendering_engine::camera_proxy_handle rendering_engine::render_world::main_camera() const
{
    const std::span<const camera_proxy> cameras = m_cameras.values();
    const camera_proxy* best = nullptr;
    camera_proxy_handle winner{};
    for (std::size_t i = 0; i < cameras.size(); ++i)
    {
        if (cameras[i].enabled && cameras[i].main && outranks(cameras[i], best))
        {
            best = &cameras[i];
            winner = m_cameras.handle_at(i);
        }
    }
    return winner;
}

void rendering_engine::render_world::set_drawable_aspect(float aspect_ratio)
{
    m_drawable_aspect = aspect_ratio > 0.0f ? aspect_ratio : 0.0f;
    if (aspect_ratio > 0.0f)
    {
        ++m_aspect_revision;
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
