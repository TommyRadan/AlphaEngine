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

#include <runtime/gltf_instantiate.hpp>

#include <string>
#include <utility>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/node.hpp>

namespace runtime
{
    namespace
    {
        namespace math = core::math;

        // glTF is +Y up, right-handed; the engine is +Z up. Rotating +90
        // degrees about X maps glTF +Y to +Z (up) and glTF +Z (the asset's
        // front) to -Y, so imported assets stand upright. Applied to each
        // root node's pose: a pure rotation R commutes past a translation as
        // R * T(t) = T(R t) * R, so the root's TRS becomes (R t, R q, s) and
        // no extra node is inserted.
        constexpr float k_half_sqrt2 = 0.70710678118f;
        constexpr math::quat k_gltf_to_engine{k_half_sqrt2, k_half_sqrt2, 0.0f, 0.0f};

        rendering_engine::material* material_for(const rendering_engine::gltf_model& model, std::size_t index)
        {
            if (index != rendering_engine::gltf_npos && index < model.materials.size() &&
                model.materials[index] != nullptr)
            {
                return model.materials[index].get();
            }
            return model.default_material.get();
        }

        // Gives @p target the mesh component drawing primitive @p index.
        void attach_primitive(const rendering_engine::gltf_model& model, std::size_t index, node& target)
        {
            const rendering_engine::gltf_mesh_primitive& primitive = model.primitives[index];
            if (primitive.mesh == nullptr)
            {
                return;
            }
            rendering_engine::material* material = material_for(model, primitive.material_index);
            if (material == nullptr)
            {
                LOG_WRN(
                    "gltf: node '%s' has no material to draw primitive %zu with; skipped", target.name.c_str(), index);
                return;
            }
            target.add_component<mesh_component>(mesh_component{material, primitive.mesh});
        }

        void spawn(const rendering_engine::gltf_model& model,
                   std::size_t index,
                   node& parent,
                   bool is_root,
                   std::vector<std::unique_ptr<node>>& owned,
                   std::vector<bool>& visited)
        {
            if (index >= model.nodes.size())
            {
                LOG_WRN("gltf: node index %zu is out of range; skipped", index);
                return;
            }
            if (visited[index])
            {
                LOG_WRN("gltf: node '%s' is reachable twice (a cycle or a shared child); skipped",
                        model.nodes[index].name.c_str());
                return;
            }
            visited[index] = true;

            const rendering_engine::gltf_node& source = model.nodes[index];
            auto spawned = std::make_unique<node>();
            spawned->name = source.name;

            math::vec3 position = source.translation;
            math::quat rotation = source.rotation;
            if (is_root)
            {
                position = k_gltf_to_engine * position;
                rotation = k_gltf_to_engine * rotation;
            }
            spawned->transform.set_position(position);
            spawned->transform.set_quaternion(rotation);
            spawned->transform.set_scale(source.scale);

            // Parent first so the node inherits the scene's component store
            // before any mesh component is attached.
            parent.add(*spawned);
            node& current = *spawned;
            owned.push_back(std::move(spawned));

            if (source.primitives.size() == 1)
            {
                attach_primitive(model, source.primitives[0], current);
            }
            else
            {
                // A node holds one component per type, so several primitives
                // fan out into one child each.
                for (std::size_t k = 0; k < source.primitives.size(); ++k)
                {
                    auto child = std::make_unique<node>();
                    child->name = source.name + "/primitive" + std::to_string(k);
                    current.add(*child);
                    attach_primitive(model, source.primitives[k], *child);
                    owned.push_back(std::move(child));
                }
            }

            for (const std::size_t child : source.children)
            {
                spawn(model, child, current, false, owned, visited);
            }
        }
    } // namespace

    std::vector<std::unique_ptr<node>> instantiate_gltf(const rendering_engine::gltf_model& model, node& parent)
    {
        std::vector<std::unique_ptr<node>> owned;
        std::vector<bool> visited(model.nodes.size(), false);
        for (const std::size_t root : model.root_nodes)
        {
            spawn(model, root, parent, true, owned, visited);
        }
        return owned;
    }
} // namespace runtime
