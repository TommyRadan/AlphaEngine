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
 * @file node.hpp
 * @brief Hierarchical scene-graph node (Three.js @c Object3D / @c Group analog).
 */

#pragma once

#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/util/transform.hpp>

namespace scene_graph
{
    /**
     * @brief A node in the scene hierarchy.
     *
     * Each node owns a local @ref rendering_engine::util::transform and a set
     * of parent/child links. Mutating @ref transform positions the node
     * relative to its parent; @ref world_matrix composes the parent chain so a
     * child inherits the accumulated transform of every ancestor — the
     * @c THREE.Object3D / @c THREE.Group model. A node carrying no renderable is
     * simply a group used to move a subtree as a unit.
     *
     * Links are non-owning raw pointers: a node never deletes its parent or its
     * children, and the caller keeps every node alive for as long as it is
     * wired into a tree. The destructor detaches the node from its parent and
     * orphans its children back to world space so dangling links never outlive
     * the node. Main-thread-only; member access is not synchronised.
     */
    struct node
    {
        node();
        ~node();

        // Non-copyable: a node holds raw parent/child links that a copy would
        // alias. Moving would invalidate the addresses children parent against.
        node(const node&) = delete;
        node& operator=(const node&) = delete;
        node(node&&) = delete;
        node& operator=(node&&) = delete;

        /**
         * @brief Local transform, relative to the parent node (or world space
         *        when this node is a root).
         *
         * Its parent pointer is kept in sync with @ref add / @ref remove, so
         * @c transform.get_world_matrix() and @ref world_matrix agree.
         */
        rendering_engine::util::transform transform;

        /**
         * @brief Attaches @p child below this node.
         *
         * Re-parents @p child (detaching it from any previous parent first) and
         * points its transform at this node so world matrices propagate. A node
         * is never added to itself.
         */
        void add(node& child);

        /** @brief Detaches @p child, returning it to world space. No-op if not a child. */
        void remove(node& child);

        /** @brief Parent node, or @c nullptr when this node is a root. */
        node* parent() const noexcept;

        /** @brief Direct children, in insertion order. */
        const std::vector<node*>& children() const noexcept;

        /**
         * @brief World-space matrix of this node.
         *
         * Equivalent to @c transform.get_world_matrix(): @c parent.world * local
         * when parented, otherwise the local matrix.
         */
        infrastructure::math::mat4 world_matrix() const;

        /**
         * @brief Attaches a renderable so its draws inherit this node's world
         *        transform.
         *
         * Parents the renderable's own transform under this node; the renderable
         * keeps its local transform and is still owned and registered with the
         * renderer by the caller. Templated so it accepts any renderable that
         * exposes a public @c transform member without naming each type here.
         */
        template<typename Object>
        void attach(Object& object)
        {
            object.transform.set_parent(&transform);
        }

        /** @brief Detaches a renderable previously passed to @ref attach. */
        template<typename Object>
        void detach(Object& object)
        {
            object.transform.set_parent(nullptr);
        }

    private:
        node* m_parent;
        std::vector<node*> m_children;
    };
} // namespace scene_graph
