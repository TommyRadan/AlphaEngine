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

#include <memory>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/mesh/mesh.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    struct model : public renderable
    {
        // The material is non-owning; it is created once by
        // @ref rendering_engine::context (e.g. @c basic_material) and
        // shared by every renderable that draws under it.
        explicit model(material* mat);
        ~model() override;

        rendering_engine::util::transform transform;

        // Uploads a private copy of @p mesh owned by this model. Prefer
        // @ref set_mesh to share a cached upload between models drawing the
        // same geometry.
        void upload_mesh(const rendering_engine::mesh& mesh);

        // Draws geometry cached by @ref asset_cache instead of uploading a
        // private copy. The model holds a reference for as long as it draws the
        // mesh; the shared GPU buffer is released once no model references it.
        // Mutually exclusive with @ref upload_mesh — use one.
        void set_mesh(std::shared_ptr<mesh_asset> mesh);

        // No-op — meshes upload through @ref upload_mesh / @ref set_mesh, which
        // are the entry points @c model uses instead of @ref renderable::upload.
        void upload() final {}

        void collect_draw_items(std::vector<draw_item>& out) final;

    private:
        material* m_material{nullptr};

        // Shared geometry from @ref asset_cache, set via @ref set_mesh. When
        // present it is drawn instead of the privately-owned
        // @ref m_vertex_buffer and is not freed here.
        std::shared_ptr<mesh_asset> m_mesh;

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};

        size_t m_vertex_count{0};
        uint32_t m_vertex_stride{0};
    };
} // namespace rendering_engine
