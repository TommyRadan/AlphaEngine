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
 * @file gltf_instantiate.hpp
 * @brief Spawns an imported glTF model as scene-graph nodes.
 */

#pragma once

#include <memory>
#include <vector>

#include <rendering_engine/assets/gltf_importer.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief Creates one @ref node per glTF node reachable from
     *        @p model's roots, parented under @p parent, drawing its
     *        primitives through @ref mesh_component.
     *
     * Each node takes the glTF node's name and local TRS. A node with one
     * primitive carries the @ref mesh_component itself; a node with several
     * gets one child node per primitive (named @c "<node>/primitive<k>"),
     * since a node holds at most one component of a type. Primitives draw
     * with @ref gltf_model::materials[material_index], or the model's
     * @ref gltf_model::default_material when they name none.
     *
     * glTF is +Y up and the engine +Z up, so every root node's pose is
     * pre-rotated +90 degrees about X (glTF +Y becomes +Z, glTF +Z — the
     * asset's front — becomes -Y) and imported assets stand upright. A pure
     * rotation commutes past a translation, so this folds into the root's
     * own TRS and no extra node is inserted.
     *
     * The nodes are caller-owned: keep the returned vector alive while they
     * are in the scene, destroy it (which unregisters every mesh) before
     * @p model, and both before the engine quits. @p parent must belong to
     * a scene (carry a component store) or the mesh components cannot be
     * attached.
     */
    std::vector<std::unique_ptr<node>> instantiate_gltf(const rendering_engine::gltf_model& model, node& parent);
} // namespace runtime
