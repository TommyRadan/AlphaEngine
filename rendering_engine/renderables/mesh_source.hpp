// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file mesh_source.hpp
 * @brief Base of the ready-made meshes a scene node draws: the premade_3d
 *        primitives, lines, point clouds and instanced meshes.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <core/math/aabb.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;
    struct render_world;

    /**
     * @brief Geometry and a material, described for a mesh proxy.
     *
     * A source holds what a proxy draws — a @ref mesh_asset, shared through
     * the asset cache or uploaded for the source alone, and a non-owning
     * material — and answers @ref describe with it. It has no placement:
     * whoever owns it (a @c runtime::renderable_component) keeps a proxy in
     * a @ref render_world and places it there. The renderer never reads a
     * source; the render extraction copies @ref describe into the proxy
     * (and, for an instanced source, @ref write_instances into the proxy's
     * instance snapshot) whenever @ref revision moved, so a change made to a
     * source reaches the frame after it.
     *
     * Geometry is uploaded when it is set, in the constructor for the
     * shapes, so a source is ready to draw once built.
     */
    struct mesh_source
    {
        virtual ~mesh_source() = default;

        mesh_source(const mesh_source&) = delete;
        mesh_source& operator=(const mesh_source&) = delete;

        /**
         * @brief What a proxy of this source draws: the geometry, the
         *        material, the object-space bounds of the geometry, the
         *        layer bits and the source's name.
         */
        virtual mesh_description describe() const;

        /**
         * @brief The drawn geometry's box in the space the source is placed
         *        in, from CPU-side data — what physics colliders fit
         *        themselves to — or none where there is no such box.
         */
        virtual std::optional<core::math::aabb> local_bounds() const;

        /**
         * @brief Captures the instance records changed since the last call
         *        into @p proxy's snapshot in @p world. Only an instanced
         *        source (@ref mesh_description::instanced) has any; the
         *        default does nothing.
         */
        virtual void write_instances(render_world& world, mesh_proxy_handle proxy);

        /**
         * @brief Advances whenever @ref describe or the instance data would
         *        answer differently, so a proxy is rewritten only after a
         *        change. Never 0.
         */
        uint64_t revision() const noexcept
        {
            return m_revision;
        }

        /** @brief The material the source draws with; non-owning, may be null. */
        material* get_material() const noexcept
        {
            return m_material;
        }

        /** @brief The geometry the source draws, or null before any is set. */
        const std::shared_ptr<mesh_asset>& mesh() const noexcept
        {
            return m_mesh;
        }

        /**
         * @brief Sets the layer bits the source's proxy belongs to (see
         *        @ref layer_default / @ref layer_editor / @ref layer_all). A
         *        camera or pass that filters by layer skips the proxy
         *        whenever @c (mask & filter_mask) == 0.
         */
        void set_layer_mask(uint32_t mask) noexcept;

        /** @brief The layer bits; @ref layer_default unless set. */
        uint32_t layer_mask() const noexcept
        {
            return m_layer_mask;
        }

    protected:
        // @p mat is non-owning; @p name labels the source's proxy in log
        // lines and must outlive it (a string literal).
        mesh_source(material* mat, const char* name) noexcept;

        // Replaces the drawn geometry.
        void set_mesh(std::shared_ptr<mesh_asset> mesh) noexcept;

        // Advances @ref revision after a change a subclass makes to what
        // it describes.
        void changed() noexcept;

        const char* name() const noexcept
        {
            return m_name;
        }

    private:
        material* m_material{nullptr};
        std::shared_ptr<mesh_asset> m_mesh;
        const char* m_name;
        uint32_t m_layer_mask{layer_default};
        uint64_t m_revision{1};
    };
} // namespace rendering_engine
