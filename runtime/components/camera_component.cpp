// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/camera_component.hpp>

#include <core/log.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/render_texture.hpp>
#include <rendering_engine/render_world.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>

namespace
{
    // The width / height of the rectangle @p camera renders into: its
    // viewport's share of its render texture, or of the drawable @p world
    // reports for a camera on the swapchain; 0 when there is nothing to
    // measure (no drawable yet, an empty rectangle).
    float view_aspect(const rendering_engine::render_world& world, const rendering_engine::camera& camera)
    {
        const rendering_engine::viewport_rect& viewport = camera.viewport();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f)
        {
            return 0.0f;
        }
        const rendering_engine::render_texture* target = camera.target();
        float target_aspect = world.drawable_aspect();
        if (target != nullptr)
        {
            target_aspect = target->height() != 0
                                ? static_cast<float>(target->width()) / static_cast<float>(target->height())
                                : 0.0f;
        }
        return target_aspect * viewport.width / viewport.height;
    }
} // namespace

runtime::camera_component::camera_component(std::unique_ptr<rendering_engine::camera> camera)
    : m_camera{std::move(camera)}
{
}

runtime::camera_component runtime::camera_component::clone() const
{
    if (!m_camera)
    {
        return camera_component{};
    }

    std::unique_ptr<rendering_engine::camera> copy;
    if (const auto* perspective = dynamic_cast<const rendering_engine::perspective_camera*>(m_camera.get()))
    {
        copy = std::make_unique<rendering_engine::perspective_camera>(perspective->get_field_of_view(),
                                                                      perspective->get_aspect_ratio(),
                                                                      perspective->get_near_clip(),
                                                                      perspective->get_far_clip());
    }
    else if (const auto* orthographic = dynamic_cast<const rendering_engine::orthographic_camera*>(m_camera.get()))
    {
        auto ortho = std::make_unique<rendering_engine::orthographic_camera>();
        ortho->set_x_magnification(orthographic->get_x_magnification());
        ortho->set_y_magnification(orthographic->get_y_magnification());
        ortho->set_near_clip(orthographic->get_near_clip());
        ortho->set_far_clip(orthographic->get_far_clip());
        copy = std::move(ortho);
    }
    else
    {
        LOG_WRN("runtime::camera_component::clone: unknown camera type; the clone owns no camera");
        return camera_component{};
    }

    copy->set_priority(m_camera->get_priority());
    copy->set_main(m_camera->is_main());
    copy->set_target(m_camera->target());
    copy->set_viewport(m_camera->viewport());
    copy->set_draws_ui(m_camera->draws_ui());
    camera_component cloned{std::move(copy)};
    cloned.m_offset.set_position(m_offset.get_position());
    cloned.m_offset.set_quaternion(m_offset.get_quaternion());
    cloned.m_offset.set_scale(m_offset.get_scale());
    return cloned;
}

core::math::mat4 runtime::camera_component::world_matrix(const node& owner) const
{
    return owner.transform.get_world_matrix() * m_offset.get_transform_matrix();
}

void runtime::camera_component::on_attach(node& owner)
{
    if (!m_camera)
    {
        return;
    }

    runtime::scene* scene = owner.scene();
    rendering_engine::render_world* world = scene != nullptr ? scene->world() : nullptr;
    if (world == nullptr)
    {
        LOG_WRN("runtime::camera_component::on_attach: node has no scene render_world; the camera has no proxy");
        return;
    }

    // Match the rectangle the camera renders into, as every later change
    // will be matched by extract().
    m_applied_aspect = view_aspect(*world, *m_camera);
    if (m_applied_aspect > 0.0f)
    {
        m_camera->set_aspect_ratio(m_applied_aspect);
    }
    m_aspect_revision = world->aspect_revision();

    m_world = world;
    m_proxy = world->create_camera(rendering_engine::camera_proxy{});
    m_node_version = 0;
    m_offset_version = 0;
    extract(owner);
}

void runtime::camera_component::on_destroy()
{
    if (m_world != nullptr)
    {
        m_world->destroy_camera(m_proxy);
    }
    m_world = nullptr;
    m_proxy = {};
}

void runtime::camera_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (!m_camera)
    {
        return;
    }
    m_camera->set_enabled(active);
    // The arbitration may be asked before the next extraction (by game
    // code finding the rendering camera), so the flag reaches the proxy now.
    if (rendering_engine::camera_proxy* proxy = m_world != nullptr ? m_world->camera(m_proxy) : nullptr)
    {
        proxy->enabled = active;
    }
}

void runtime::camera_component::extract(const node& owner)
{
    if (m_world == nullptr || !m_camera)
    {
        return;
    }
    rendering_engine::camera_proxy* proxy = m_world->camera(m_proxy);
    if (proxy == nullptr)
    {
        return;
    }

    // A drawable size reported since the camera last took one, or another
    // rectangle to render into (a new target or viewport).
    const float aspect = view_aspect(*m_world, *m_camera);
    if (m_world->aspect_revision() != m_aspect_revision || aspect != m_applied_aspect)
    {
        m_aspect_revision = m_world->aspect_revision();
        m_applied_aspect = aspect;
        if (aspect > 0.0f)
        {
            m_camera->set_aspect_ratio(aspect);
        }
    }

    proxy->culling_mask = m_camera->culling_mask();
    const rendering_engine::render_texture* target = m_camera->target();
    proxy->target = target != nullptr ? target->target() : rendering_engine::gpu::render_target{};
    proxy->target_width = target != nullptr ? target->width() : 0;
    proxy->target_height = target != nullptr ? target->height() : 0;
    proxy->viewport = m_camera->viewport();
    proxy->ui = m_camera->draws_ui();
    proxy->priority = m_camera->get_priority();
    proxy->main = m_camera->is_main();
    proxy->enabled = m_camera->is_enabled();

    const uint64_t node_version = owner.transform.get_world_version();
    const uint64_t offset_version = m_offset.get_world_version();
    const bool moved = node_version != m_node_version || offset_version != m_offset_version;
    if (moved)
    {
        proxy->world = world_matrix(owner);
        proxy->view = rendering_engine::view_matrix_from_world(proxy->world);
        m_node_version = node_version;
        m_offset_version = offset_version;
    }

    // The camera caches its projection, so this is a copy unless the lens or
    // the aspect changed.
    const core::math::mat4 projection = m_camera->get_projection_matrix();
    if (moved || projection != proxy->projection)
    {
        proxy->projection = projection;
        proxy->frustum = core::math::frustum::from_view_projection(projection * proxy->view);
    }
}
