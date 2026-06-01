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

#include <scene_graph/mesh_component.hpp>

#include <control/engine.hpp>
#include <rendering_engine/mesh/mesh.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <scene_graph/node.hpp>

scene_graph::mesh_component::mesh_component(rendering_engine::material* material, const rendering_engine::mesh& mesh)
    : m_model{std::make_unique<rendering_engine::model>(material)}
{
    m_model->upload_mesh(mesh);
}

void scene_graph::mesh_component::on_attach(node& owner)
{
    if (!m_model)
    {
        return;
    }

    // Draw at the node's world transform: the model's local transform stays
    // identity and inherits the node pose through the transform parent chain.
    m_model->transform.set_parent(&owner.transform);
    control::current_engine().renderer->register_scene_renderable(m_model.get());
    m_registered = true;
}

void scene_graph::mesh_component::on_destroy()
{
    if (m_registered && m_model)
    {
        control::current_engine().renderer->unregister_scene_renderable(m_model.get());
        m_registered = false;
    }
}
