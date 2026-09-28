// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/render_world.hpp>

#include <algorithm>
#include <cassert>
#include <utility>

#include <core/log.hpp>
#include <core/math/aabb.hpp>
#include <rendering_engine/lighting/environment_probe.hpp>
#include <rendering_engine/renderables/vertex_format_check.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

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
    m_debug_draw.clear();
}

void rendering_engine::render_world::begin_frame() noexcept
{
    m_in_frame = true;
}

void rendering_engine::render_world::end_frame() noexcept
{
    m_in_frame = false;
}

rendering_engine::mesh_proxy_handle rendering_engine::render_world::create_mesh(const mesh_description& source,
                                                                                const core::math::mat4& world)
{
    assert(!m_in_frame && "mesh proxies are created between frames");
    const mesh_proxy_handle created = m_meshes.insert(mesh_proxy{});
    set_mesh_source(created, source);
    set_mesh_world(created, world);
    return created;
}

void rendering_engine::render_world::destroy_mesh(mesh_proxy_handle mesh)
{
    assert(!m_in_frame && "mesh proxies are destroyed between frames");
    m_meshes.erase(mesh);
}

void rendering_engine::render_world::set_mesh_source(mesh_proxy_handle mesh, const mesh_description& source)
{
    mesh_proxy* proxy = m_meshes.get(mesh);
    if (proxy == nullptr)
    {
        return;
    }

    // A mismatch or a missing material is reported once per geometry and
    // material the proxy is given, not on every write of the same pair.
    if (proxy->source.mesh != source.mesh || proxy->source.mat != source.mat)
    {
        proxy->format_reported = false;
        if (source.mat == nullptr)
        {
            LOG_WRN("%s: no material; nothing is drawn", source.name);
        }
    }
    proxy->source = source;
    proxy->format_ok = true;
    if (source.check_format && source.mat != nullptr && source.mesh != nullptr)
    {
        proxy->format_ok = validate_vertex_format(*source.mat, *source.mesh, source.name, proxy->format_reported);
    }

    if (source.bounds.has_value())
    {
        proxy->world_bounds = source.placed ? core::math::transform(*source.bounds, proxy->world) : *source.bounds;
    }
}

void rendering_engine::render_world::set_mesh_world(mesh_proxy_handle mesh, const core::math::mat4& world)
{
    mesh_proxy* proxy = m_meshes.get(mesh);
    if (proxy == nullptr)
    {
        return;
    }
    proxy->world = world;
    proxy->per_draw = make_per_draw_payload(world);
    proxy->mirrored = is_mirrored(proxy->per_draw.model);
    if (proxy->source.bounds.has_value() && proxy->source.placed)
    {
        proxy->world_bounds = core::math::transform(*proxy->source.bounds, world);
    }
}

void rendering_engine::render_world::set_mesh_visible(mesh_proxy_handle mesh, bool visible)
{
    if (mesh_proxy* proxy = m_meshes.get(mesh))
    {
        proxy->visible = visible;
    }
}

void rendering_engine::render_world::set_mesh_joints(mesh_proxy_handle mesh, std::span<const core::math::mat4> joints)
{
    if (mesh_proxy* proxy = m_meshes.get(mesh))
    {
        proxy->joints.assign(joints.begin(), joints.end());
        ++proxy->joints_revision;
    }
}

void rendering_engine::render_world::write_mesh_instances(mesh_proxy_handle mesh,
                                                          std::span<const mesh_instance> records,
                                                          uint32_t changed_begin,
                                                          uint32_t changed_end,
                                                          const mesh_indirect_args& args)
{
    mesh_proxy* proxy = m_meshes.get(mesh);
    if (proxy == nullptr)
    {
        return;
    }
    mesh_instances& snapshot = proxy->instances;
    const auto slots = static_cast<uint32_t>(records.size());
    if (snapshot.records.size() != records.size())
    {
        // A new slot count: every record is copied, and the renderer
        // reallocates its stream and uploads it whole on its own.
        snapshot.records.assign(records.begin(), records.end());
        changed_begin = 0;
        changed_end = slots;
    }
    else
    {
        changed_end = std::min(changed_end, slots);
        if (changed_begin < changed_end)
        {
            std::copy(records.begin() + changed_begin,
                      records.begin() + changed_end,
                      snapshot.records.begin() + changed_begin);
        }
    }
    if (changed_begin < changed_end)
    {
        if (snapshot.dirty_begin == snapshot.dirty_end)
        {
            snapshot.dirty_begin = changed_begin;
            snapshot.dirty_end = changed_end;
        }
        else
        {
            snapshot.dirty_begin = std::min(snapshot.dirty_begin, changed_begin);
            snapshot.dirty_end = std::max(snapshot.dirty_end, changed_end);
        }
    }
    snapshot.args = args;
}

rendering_engine::ui_proxy_handle rendering_engine::render_world::create_ui_element(ui_element_data data)
{
    assert(!m_in_frame && "UI proxies are created between frames");
    ui_proxy proxy{};
    proxy.data = std::move(data);
    proxy.revision = 1;
    return m_ui_elements.insert(std::move(proxy));
}

void rendering_engine::render_world::destroy_ui_element(ui_proxy_handle element)
{
    assert(!m_in_frame && "UI proxies are destroyed between frames");
    m_ui_elements.erase(element);
}

void rendering_engine::render_world::set_ui_element(ui_proxy_handle element, ui_element_data data)
{
    if (ui_proxy* proxy = m_ui_elements.get(element))
    {
        proxy->data = std::move(data);
        ++proxy->revision;
    }
}

const rendering_engine::ui_proxy* rendering_engine::render_world::ui_element(ui_proxy_handle element) const noexcept
{
    return m_ui_elements.get(element);
}

rendering_engine::mesh_proxy* rendering_engine::render_world::mesh(mesh_proxy_handle mesh) noexcept
{
    return m_meshes.get(mesh);
}

const rendering_engine::mesh_proxy* rendering_engine::render_world::mesh(mesh_proxy_handle mesh) const noexcept
{
    return m_meshes.get(mesh);
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
