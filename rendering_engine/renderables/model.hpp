// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <core/math/aabb.hpp>
#include <core/math/mat4.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/assets/vertex.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/per_draw_ring.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;
    struct mesh_data;

    struct model : public renderable
    {
        // The material is non-owning; it is created once by
        // @ref rendering_engine::renderer (e.g. @c basic_material) and
        // shared by every renderable that draws under it.
        explicit model(material* mat);
        ~model() override;

        core::transform transform;

        // Uploads a private copy of @p mesh owned by this model: its vertex
        // records (stride and format as the data declares them) and, when it
        // carries any, its indices, which the model then draws indexed.
        // Prefer @ref set_mesh to share a cached upload between models
        // drawing the same geometry.
        void upload_mesh(const mesh_data& mesh);

        // Draws geometry cached by @ref asset_cache instead of uploading a
        // private copy. The model holds a reference for as long as it draws the
        // mesh; the shared GPU buffer is released once no model references it.
        // An asset with an index buffer is drawn indexed. Mutually exclusive
        // with @ref upload_mesh — use one.
        void set_mesh(std::shared_ptr<mesh_asset> mesh);

        // The material this model draws with, or @c nullptr for a
        // default-constructed model. Non-owning; read-only accessor for
        // tooling (the debug overlay's inspector).
        material* get_material() const noexcept
        {
            return m_material;
        }

        // No-op — meshes upload through @ref upload_mesh / @ref set_mesh, which
        // are the entry points @c model uses instead of @ref renderable::upload.
        void upload() final {}

        void collect_draw_items(std::vector<draw_item>& out) final;

        // The drawn mesh's object-space box under @ref transform: the cached
        // asset's bounds, or the box computed over the vertices at
        // @ref upload_mesh. False until either has supplied geometry, and
        // false while the material skins: the bind-pose box does not bound
        // the animated pose, so a skinned model is never frustum-culled.
        bool world_bounds(core::math::aabb& out) const final;

        // The same box under @ref transform's own matrix only (the bind
        // pose, for a skinned mesh, even while world_bounds declines).
        bool local_bounds(core::math::aabb& out) const final;

        // The joint palette a skinned material draws the mesh with: one
        // matrix per joint the vertices' joint indices name, each mapping
        // the mesh's bind-pose space onto that joint's current pose in the
        // model's own space (so @ref transform still places the result).
        // Copied here and uploaded to the per-draw storage buffer at the
        // next draw. Written each frame by the animation system
        // (@c runtime::animator_component); ignored while the material does
        // not skin. A model whose material skins draws nothing until a
        // palette has been set.
        void set_joint_matrices(std::span<const core::math::mat4> matrices);

        // Joints in the current palette (0 before the first
        // @ref set_joint_matrices).
        size_t joint_count() const;

        // False while the material skins: the depth-only shadow pipelines
        // have no skinned variant, so the model would cast its bind pose
        // (and its per-draw group does not match their layout).
        bool casts_shadow() const override;

    private:
        // Point @p item at the skinned per-draw group, (re)building it,
        // the joint palette buffer and the block as needed. False when the
        // group could not be created.
        bool bind_skinned(draw_item& item);

        material* m_material{nullptr};

        // Shared geometry from @ref asset_cache, set via @ref set_mesh. When
        // present it is drawn instead of the privately-owned
        // @ref m_vertex_buffer and is not freed here.
        std::shared_ptr<mesh_asset> m_mesh;

        gpu::buffer m_vertex_buffer{};

        // Indices of the private upload, when its @ref mesh_data carried any;
        // invalid otherwise, and the private buffer is drawn as a plain
        // vertex array.
        gpu::buffer m_index_buffer{};
        uint32_t m_index_count{0};

        // The PerDraw block, recomputed when the transform moves, and its
        // slot in the per-draw ring while the material is rigid (on a
        // device without push constants).
        per_draw_binding m_per_draw;

        // A skinned draw's own per-draw group: its joint palette cannot
        // live in the ring's shared group, so the block sits in a private
        // uniform buffer beside it, bound without a dynamic offset and
        // rewritten only when @ref m_draw_ubo_version falls behind the
        // block. Allocated on the first skinned draw. On a device with
        // push constants the block is pushed instead and the buffer is
        // never written.
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
        uint64_t m_draw_ubo_version{0};

        // The layout @ref m_draw_bind_group was built against; a material
        // that switches skinned variants changes it, and the group is
        // rebuilt.
        gpu::bind_group_layout m_draw_bind_group_layout{};

        // The skinning palette (see @ref set_joint_matrices), its storage
        // buffer and the number of matrices the buffer has room for.
        std::vector<core::math::mat4> m_joint_matrices;
        gpu::buffer m_joint_buffer{};
        size_t m_joint_capacity{0};
        bool m_joints_dirty{false};
        bool m_missing_joints_reported{false};

        size_t m_vertex_count{0};
        uint32_t m_vertex_stride{0};

        // Object-space bounds of the private @ref m_vertex_buffer, computed
        // at @ref upload_mesh; the cached-mesh path reads the asset's box
        // instead.
        core::math::aabb m_local_bounds{};
        bool m_has_local_bounds{false};

        // Record layout of whichever vertex buffer is drawn, checked against
        // the material before every draw (see @ref validate_vertex_format).
        vertex_format m_vertex_format{vertex_format::custom};
        bool m_vertex_format_reported{false};
    };
} // namespace rendering_engine
