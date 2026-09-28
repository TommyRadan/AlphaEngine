// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/infinite_grid.hpp>

#include <vector>

#include <assets/mesh_data.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/materials/grid_material.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

namespace rendering_engine::debug_draw
{
    infinite_grid::infinite_grid(renderer& owner, float fade_distance)
        : m_material(owner.create_grid_material(fade_distance)), m_world(&owner.world())
    {
        // A single fullscreen triangle in clip space; the grid material's
        // vertex shader unprojects these corners to reconstruct the view
        // rays, so no transform is applied to the positions themselves.
        const std::vector<core::math::vec3> vertices{core::math::vec3{-1.0f, -1.0f, 0.0f},
                                                     core::math::vec3{3.0f, -1.0f, 0.0f},
                                                     core::math::vec3{-1.0f, 3.0f, 0.0f}};
        m_mesh = upload_mesh(owner.device(), assets::mesh_data::from_vertices(vertices), assets::vertex_format::custom);

        // The proxy carries the identity model of the origin grid (see
        // per_draw_ubo.hpp); the shader references the model matrix when
        // reconstructing depth. Its clip-space triangle has no world box to
        // cull by, and fed to the depth-only shadow pipelines it would write
        // a phantom occluder.
        mesh_description description{};
        description.mesh = m_mesh;
        description.mat = m_material.get();
        description.layer_mask = layer_editor;
        description.casts_shadow = false;
        description.check_format = false;
        description.name = "infinite_grid";
        m_proxy = m_world->create_mesh(description, core::math::mat4{});
    }

    infinite_grid::~infinite_grid()
    {
        m_world->destroy_mesh(m_proxy);
        m_mesh.reset();
        m_material.reset();
    }

    void infinite_grid::set_visible(bool visible)
    {
        m_visible = visible;
        m_world->set_mesh_visible(m_proxy, visible);
    }
} // namespace rendering_engine::debug_draw
