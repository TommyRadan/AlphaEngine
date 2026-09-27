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

#include <functional>
#include <memory>
#include <vector>

#include <core/subscription.hpp>
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
     * The conversion fixes the up axis only. An asset's front ends up facing
     * -Y rather than the engine's +X forward (core/math/math.hpp), which a
     * mesh does not care about; a glTF camera or KHR light, which looks down
     * its node's -Z (engine +Y after the conversion), would need a further
     * local rotation onto +X. Neither is instantiated today; when they are,
     * that rotation belongs here beside the root pre-rotation, not in the
     * components.
     *
     * A skinned primitive of a node with a skin draws with the model's
     * skinned material twin. When the model has animations or skinned
     * meshes, the first spawned root also gets an @ref animator_component
     * over @ref rendering_engine::gltf_model::node_skeleton: every spawned
     * node is bound to its joint (a root through the same +90 degree turn),
     * every skinned mesh node to its skin, and the first clip, if any, is
     * started looping — call @c stop() or @c play() on the component to
     * change that. The animator is disabled along with that root node.
     *
     * The nodes are owned by @p parent's scene (made with
     * @c context::create_node), and the returned vector names the spawned
     * root nodes. The mesh and animator components draw with @p model's
     * materials and skeleton, so the nodes must be gone before @p model is
     * destroyed: @c destroy_node the roots (a subtree goes with its root, and
     * frees the animator's node bindings along with it) or unload the scene
     * first. @p parent must belong to a scene, and that scene must not be
     * mid-traversal (the components could not be attached); otherwise
     * nothing is spawned.
     */
    std::vector<node*> instantiate_gltf(const rendering_engine::gltf_model& model, node& parent);

    /**
     * @brief Instantiates @p asset's model under @p parent through
     *        @ref instantiate_gltf as soon as it has loaded (see
     *        @c asset_cache::load_gltf_async).
     *
     * The check runs on @c core::render_update, which the engine emits
     * outside every scene traversal, so the nodes can be created there: an
     * asset that is already ready spawns on the next tick, one still loading
     * on the first tick after @c asset_cache::pump resolves it. @p on_spawned,
     * when set, then receives the spawned roots. A load that failed spawns
     * nothing (the cache logged why). Either way it happens once.
     *
     * The returned subscription is the pending spawn: keep it alive where it
     * cannot outlive @p parent — in a behaviour on @p parent or one of its
     * ancestors — and destroy it to cancel. It holds @p asset until then;
     * as with @ref instantiate_gltf, the model must outlive the nodes
     * spawned from it.
     */
    core::subscription
    instantiate_gltf_when_ready(std::shared_ptr<const rendering_engine::gltf_asset> asset,
                                node& parent,
                                std::function<void(const std::vector<node*>& roots)> on_spawned = {});
} // namespace runtime
