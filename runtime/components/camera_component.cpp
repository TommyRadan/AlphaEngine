/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <runtime/components/camera_component.hpp>

#include <core/log.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <runtime/node.hpp>

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

    // The camera's own transform is its local offset under the node.
    copy->transform.set_position(m_camera->transform.get_position());
    copy->transform.set_quaternion(m_camera->transform.get_quaternion());
    copy->transform.set_scale(m_camera->transform.get_scale());
    copy->set_priority(m_camera->get_priority());
    copy->set_main(m_camera->is_main());
    return camera_component{std::move(copy)};
}

void runtime::camera_component::on_attach(node& owner)
{
    if (!m_camera)
    {
        return;
    }

    // View from the node's world pose: the camera's own transform is a local
    // offset that inherits the node pose through the transform parent chain,
    // and the view matrix is derived from the composed world matrix on every
    // query, so nothing has to be copied per frame.
    m_camera->transform.set_parent(&owner.transform);
    m_camera->attach();
}

void runtime::camera_component::on_destroy()
{
    if (!m_camera)
    {
        return;
    }

    m_camera->detach();
    // The owning node may outlive this component (remove_component, store
    // teardown); do not leave the camera's transform pointing at it.
    m_camera->transform.set_parent(nullptr);
}

void runtime::camera_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (m_camera)
    {
        // The camera stays attached, so the arbitration promotes it again
        // the moment the node is re-enabled (unless a higher-priority or
        // later-attached peer has since taken over).
        m_camera->set_enabled(active);
    }
}
