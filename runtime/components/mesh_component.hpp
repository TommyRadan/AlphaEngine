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

/**
 * @file mesh_component.hpp
 * @brief Component that draws a mesh at its node's world transform.
 */

#pragma once

#include <memory>

#include <rendering_engine/renderables/model.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh;
    struct mesh_asset;
} // namespace rendering_engine

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a visible mesh.
     *
     * Owns a @ref rendering_engine::model on the heap — a stable address that
     * stays valid as the component is relocated within its pool — and, while
     * attached, registers that model with the scene renderer and parents it
     * under the owning node so it draws at the node's world transform. The node
     * holds the @ref component_handle; this component (pooled in the scene's
     * @ref component_store) owns the model and its renderer registration, so
     * destroying the node — or removing the component — unregisters and frees
     * the model automatically.
     *
     * The renderer keeps a non-owning pointer to the model, which is why the
     * model lives behind a @c unique_ptr rather than inline: the registration
     * is keyed off that heap address and is unaffected when the pool moves the
     * component.
     */
    struct mesh_component
    {
        /** @brief Empty component — owns no model and draws nothing. */
        mesh_component() = default;

        /**
         * @brief Builds a model from @p mesh drawn with @p material.
         *
         * Uploads the mesh immediately; the model is registered for drawing
         * later, in @ref on_attach, once the owning node is known.
         */
        mesh_component(rendering_engine::material* material, const rendering_engine::mesh& mesh);

        /**
         * @brief Builds a model drawing a cached @p mesh with @p material.
         *
         * Shares one GPU upload across every component handed the same
         * @ref rendering_engine::mesh_asset (e.g. many bodies built from one
         * sphere), rather than uploading a private copy per component. The model
         * is registered for drawing later, in @ref on_attach.
         */
        mesh_component(rendering_engine::material* material, std::shared_ptr<rendering_engine::mesh_asset> mesh);

        /**
         * @brief Builds a model drawing a cached @p mesh with @p material, and
         *        shares ownership of @p material, so it lives as long as this
         *        component (or a clone of it) draws with it.
         *
         * The form a loaded scene uses (runtime/scene_serializer.hpp): its
         * materials belong to the components that draw with them rather than
         * to the code that built the scene. With a null @p material no model
         * is built, since it would have nothing to draw with, but @p mesh is
         * still kept (see @ref mesh).
         */
        mesh_component(std::shared_ptr<rendering_engine::material> material,
                       std::shared_ptr<rendering_engine::mesh_asset> mesh);

        /**
         * @brief Wires the model into the scene — parents it under @p owner and
         *        registers it with the scene renderer.
         *
         * Called by @ref node::add_component. Safe to leave the model relocating
         * afterwards: the registration uses the model's stable heap address.
         */
        void on_attach(node& owner);

        /**
         * @brief Unregisters the model from the renderer.
         *
         * Called by the component store immediately before this component's
         * pool slot — and the model it owns — is freed.
         */
        void on_destroy();

        /**
         * @brief Shows (registers) or hides (unregisters) the model when the
         *        owning node is enabled/disabled.
         *
         * Called by @ref node::set_active. Hiding leaves the model and its
         * parenting intact so re-enabling simply re-registers it.
         */
        void on_active_changed(node& owner, bool active);

        /**
         * @brief A new component drawing the same cached mesh with the same
         *        material, for @c scene::clone.
         *
         * Only a component built from a @ref rendering_engine::mesh_asset can
         * be cloned — the copy shares the upload; one built from a private
         * mesh upload has no source data left to copy, so its clone is empty
         * (with a warning).
         */
        mesh_component clone() const;

        /** @brief The owned model, or @c nullptr for an empty component. */
        rendering_engine::model* model() const noexcept
        {
            return m_model.get();
        }

        /** @brief The material the model draws with, or @c nullptr. */
        rendering_engine::material* material() const noexcept
        {
            return m_material;
        }

        /**
         * @brief The cached mesh the model draws, or @c nullptr for an empty
         *        component or one built from a private upload.
         */
        const std::shared_ptr<rendering_engine::mesh_asset>& mesh() const noexcept
        {
            return m_mesh;
        }

        /** @brief The material this component shares ownership of, or @c nullptr when it was handed a plain pointer. */
        const std::shared_ptr<rendering_engine::material>& owned_material() const noexcept
        {
            return m_owned_material;
        }

    private:
        void register_model();
        void unregister_model();

        // Declared before the model so it outlives it.
        std::shared_ptr<rendering_engine::material> m_owned_material;
        std::unique_ptr<rendering_engine::model> m_model;
        // What the model was built from, kept for clone(): the material and,
        // for the cached-mesh constructor, the shared asset.
        rendering_engine::material* m_material{nullptr};
        std::shared_ptr<rendering_engine::mesh_asset> m_mesh;
        bool m_registered{false};
    };
} // namespace runtime
