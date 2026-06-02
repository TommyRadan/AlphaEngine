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

        /** @brief The owned model, or @c nullptr for an empty component. */
        rendering_engine::model* model() const noexcept
        {
            return m_model.get();
        }

    private:
        void register_model();
        void unregister_model();

        std::unique_ptr<rendering_engine::model> m_model;
        bool m_registered{false};
    };
} // namespace runtime
