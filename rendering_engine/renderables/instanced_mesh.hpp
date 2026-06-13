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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    // Draws one shared mesh many times in a single instanced draw, each
    // copy with its own world transform and tint — the engine analog of
    // THREE.InstancedMesh. The geometry (vertex + index buffers) and the
    // material are shared across every instance; a per-instance vertex
    // stream of @c {mat4 model; vec4 color;} records supplies what varies.
    //
    // The renderable emits a single indexed-indirect @ref draw_item: the
    // command record carries the instance count, and the per-instance
    // stream is bound to vertex slot 1 and stepped once per instance
    // (@c glVertexAttribDivisor / @c VK_VERTEX_INPUT_RATE_INSTANCE), so the
    // whole batch costs one draw call without relying on @c gl_InstanceIndex.
    // It must be fronted by an @ref instanced_material; pass that material
    // to the constructor.
    struct instanced_mesh : public renderable
    {
        // @p mat is non-owning and is expected to be an
        // @ref instanced_material. @p instance_count is the fixed capacity
        // of the per-instance buffer; the active draw count starts equal to
        // it and can be lowered via @ref set_instance_count.
        instanced_mesh(material* mat, uint32_t instance_count);
        ~instanced_mesh() override;

        // Upload the shared geometry drawn once per instance. Vertices use
        // the position+uv+normal record (the instanced material reads only
        // the position); indices are 32-bit. Call once after construction,
        // before the first frame. Allocates buffers owned by this renderable;
        // prefer @ref set_geometry to share a cached upload between several
        // instanced meshes.
        void upload_geometry(const std::vector<vertex_position_uv_normal>& vertices,
                             const std::vector<uint32_t>& indices);

        // Draw a mesh cached by @ref asset_cache instead of uploading a private
        // copy. The shared geometry's lifetime is tied to the handle: this
        // renderable holds a reference for as long as it draws it, and the GPU
        // buffers are released when the last instanced mesh referencing them is
        // destroyed. Mutually exclusive with @ref upload_geometry — use one.
        void set_geometry(std::shared_ptr<mesh_asset> mesh);

        // Geometry uploads through @ref upload_geometry; this is a no-op so
        // @ref instanced_mesh still satisfies the @ref renderable interface.
        void upload() final {}

        void collect_draw_items(std::vector<draw_item>& out) final;

        // Fixed per-instance buffer capacity set at construction.
        uint32_t instance_capacity() const;

        // Number of instances drawn this frame. Clamped to the capacity.
        void set_instance_count(uint32_t count);
        uint32_t instance_count() const;

        // Per-instance world transform. @p index must be < the capacity.
        void set_instance_transform(uint32_t index, const core::math::mat4& transform);

        // Per-instance tint, multiplied by the material's flat colour.
        // Defaults to opaque white. @p index must be < the capacity.
        void set_instance_color(uint32_t index, const util::color& color);

    private:
        // Per-instance record mirrored on the CPU and uploaded into the
        // per-instance vertex stream. Layout matches the instanced
        // material's slot-1 vertex attributes: a mat4 model (four vec4
        // columns, 64 bytes) followed by a vec4 colour (16 bytes), 80 bytes
        // with no trailing padding.
        struct instance_record
        {
            core::math::mat4 model{};
            core::math::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        };

        material* m_material{nullptr};
        uint32_t m_capacity{0};
        uint32_t m_instance_count{0};

        // CPU mirror of the per-instance vertex stream, re-uploaded when an
        // instance changes.
        std::vector<instance_record> m_instances;
        bool m_instances_dirty{true};

        // Instance count last written into the indirect command, so the
        // command is only rewritten when the active count actually changes.
        uint32_t m_uploaded_instance_count{0};

        // Shared geometry from @ref asset_cache, set via @ref set_geometry. When
        // present its buffers are drawn instead of the privately-owned
        // @ref m_vertex_buffer / @ref m_index_buffer, and it is not freed here —
        // the shared_ptr releases it once no instanced mesh references it.
        std::shared_ptr<mesh_asset> m_mesh;

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_index_buffer{};
        gpu::buffer m_instance_buffer{};
        gpu::buffer m_indirect_buffer{};

        uint32_t m_index_count{0};
        uint32_t m_vertex_stride{0};
    };
} // namespace rendering_engine
