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

#include <scene_graph/camera_component.hpp>

#include <infrastructure/math/math.hpp>
#include <scene_graph/node.hpp>

scene_graph::camera_component::camera_component(std::unique_ptr<rendering_engine::camera> camera)
    : m_camera{std::move(camera)}
{
}

void scene_graph::camera_component::on_attach(node& owner)
{
    (void)owner;
    if (m_camera)
    {
        m_camera->attach();
        m_attached = true;
    }
}

void scene_graph::camera_component::on_destroy()
{
    if (m_attached && m_camera && rendering_engine::camera::get_current_camera() == m_camera.get())
    {
        m_camera->detach();
    }
    m_attached = false;
}

void scene_graph::camera_component::on_update(node& owner)
{
    if (!m_camera)
    {
        return;
    }

    // The camera builds its view from its own transform position (plus a
    // forward-direction rotation), so place it at the node's world translation
    // and let the owner set orientation via camera::look_at.
    const infrastructure::math::mat4 world = owner.world_matrix();
    m_camera->transform.set_position(infrastructure::math::vec3{world.m[12], world.m[13], world.m[14]});
    m_camera->invalidate_view_matrix();
}
