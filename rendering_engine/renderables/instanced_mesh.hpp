// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <assets/color.hpp>
#include <assets/vertex.hpp>
#include <core/math/aabb.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct material;
    struct mesh_asset;

    // Draws one shared mesh many times in a single instanced draw, each
    // copy with its own world transform and tint. The geometry (vertex +
    // index buffers) and the material are shared across every instance; a
    // per-instance vertex stream of @ref mesh_instance records supplies what
    // varies.
    //
    // The source describes a single indexed-indirect draw: the indirect
    // record carries the instance count, and the per-instance stream is
    // bound to vertex slot 1 and stepped once per instance
    // (@c VK_VERTEX_INPUT_RATE_INSTANCE), so the whole batch costs one draw
    // call without relying on @c gl_InstanceIndex. The records live here, on
    // the CPU; the render extraction captures the ones changed since the
    // last frame, with the draw's arguments, into the proxy's snapshot
    // (@ref write_instances), and the renderer uploads them from there. The
    // instances carry world transforms, so the proxy's own placement is
    // ignored. It must be fronted by an @ref instanced_material; pass that
    // material to the constructor.
    struct instanced_mesh : public mesh_source
    {
        // @p mat is non-owning and is expected to be an
        // @ref instanced_material. @p instance_count is the initial
        // capacity of the per-instance records (see @ref reserve_instances);
        // the active draw count starts equal to it and can be lowered via
        // @ref set_instance_count. Geometry given to @ref upload_geometry is
        // uploaded to @p device.
        instanced_mesh(gpu::device& device, material* mat, uint32_t instance_count);

        // Upload the shared geometry drawn once per instance, as a private
        // mesh this instanced mesh alone draws. Vertices use the
        // position+uv+normal record (the instanced material reads only the
        // position); indices are 32-bit. Prefer @ref set_geometry to share a
        // cached upload between several instanced meshes.
        void upload_geometry(const std::vector<assets::vertex_position_uv_normal>& vertices,
                             const std::vector<uint32_t>& indices);

        // Draw a mesh cached by @ref asset_cache instead of uploading a private
        // copy. The shared geometry's lifetime is tied to the handle: this
        // source holds a reference for as long as it draws it, and the GPU
        // buffers are released when the last holder is gone.
        void set_geometry(std::shared_ptr<mesh_asset> mesh);

        // The geometry, the material, an indexed-indirect draw of the
        // instance snapshot, and the world-space union of the geometry's box
        // under every active instance transform (none until geometry is set
        // or while the instance count is zero).
        mesh_description describe() const override;

        // None: the instances carry world transforms, so there is no box in
        // the space of whoever places the source.
        std::optional<core::math::aabb> local_bounds() const override;

        // Captures the records changed since the last call, and the draw's
        // arguments, into @p proxy's snapshot in @p world.
        void write_instances(render_world& world, mesh_proxy_handle proxy) override;

        // Per-instance record capacity: the construction count, raised by
        // @ref reserve_instances.
        uint32_t instance_capacity() const;

        // Grow the capacity to at least @p capacity records (never
        // shrinks). New records start as an identity transform with a
        // white tint; the active count is left alone, so raise it with
        // @ref set_instance_count.
        void reserve_instances(uint32_t capacity);

        // Number of instances drawn. Clamped to the capacity.
        void set_instance_count(uint32_t count);
        uint32_t instance_count() const;

        // Per-instance world transform. @p index must be < the capacity.
        void set_instance_transform(uint32_t index, const core::math::mat4& transform);

        // Per-instance tint, multiplied by the material's flat colour.
        // Defaults to opaque white. @p index must be < the capacity.
        void set_instance_color(uint32_t index, const assets::color& color);

    private:
        // Widen the changed span to cover record @p index.
        void mark_dirty(uint32_t index);

        // The device private geometry is uploaded to; it outlives the source.
        gpu::device* m_device{nullptr};
        uint32_t m_instance_count{0};

        // The records, one per instance slot. Only the span changed since
        // the last capture, [m_dirty_begin, m_dirty_end), is copied into the
        // proxy's snapshot; an empty span copies nothing.
        std::vector<mesh_instance> m_instances;
        uint32_t m_dirty_begin{0};
        uint32_t m_dirty_end{0};

        // The world-space union @ref describe reports, rebuilt only after an
        // instance transform, the instance count or the geometry changed.
        mutable core::math::aabb m_world_bounds{};
        mutable bool m_world_bounds_dirty{true};
    };
} // namespace rendering_engine
