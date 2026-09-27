// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/gltf_instantiate.hpp>

#include <algorithm>
#include <string>
#include <utility>

#include <core/event.hpp>
#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <runtime/components/animator_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>

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

        // The skinning twin of material_for, or null when the model has none.
        rendering_engine::material* skinned_material_for(const rendering_engine::gltf_model& model, std::size_t index)
        {
            if (index == rendering_engine::gltf_npos)
            {
                return model.skinned_default_material.get();
            }
            return index < model.skinned_materials.size() ? model.skinned_materials[index].get() : nullptr;
        }

        // What the spawn walk records for the animator: the node spawned for
        // each glTF node, which of them are roots (and so carry the up-axis
        // turn), and the nodes drawing a skinned primitive of each glTF node.
        struct spawn_context
        {
            const rendering_engine::gltf_model& model;
            runtime::scene& scene;
            std::vector<node*>& roots;
            std::vector<bool> visited;
            std::vector<node*> spawned;
            std::vector<bool> is_root;
            std::vector<std::vector<node*>> skinned_meshes;

            spawn_context(const rendering_engine::gltf_model& in_model,
                          runtime::scene& in_scene,
                          std::vector<node*>& in_roots)
                : model{in_model}, scene{in_scene}, roots{in_roots}, visited(in_model.nodes.size(), false),
                  spawned(in_model.nodes.size(), nullptr), is_root(in_model.nodes.size(), false),
                  skinned_meshes(in_model.nodes.size())
            {
            }
        };

        // Gives @p target the mesh component drawing primitive @p index of
        // glTF node @p source_index. A skinned primitive of a node with a skin
        // draws with the skinned material and is recorded for the animator.
        void attach_primitive(spawn_context& ctx, std::size_t source_index, std::size_t index, node& target)
        {
            const rendering_engine::gltf_model& model = ctx.model;
            const rendering_engine::gltf_mesh_primitive& primitive = model.primitives[index];
            if (primitive.mesh == nullptr)
            {
                return;
            }
            const bool skinned = primitive.skinned && model.nodes[source_index].skin != rendering_engine::gltf_npos &&
                                 model.node_skeleton != nullptr &&
                                 model.nodes[source_index].skin < model.node_skeleton->skins().size();
            rendering_engine::material* material =
                skinned ? skinned_material_for(model, primitive.material_index) : nullptr;
            if (material == nullptr)
            {
                material = material_for(model, primitive.material_index);
            }
            else
            {
                ctx.skinned_meshes[source_index].push_back(&target);
            }
            if (material == nullptr)
            {
                LOG_WRN("gltf: node '%s' has no material to draw primitive %zu with; skipped",
                        target.name().c_str(),
                        index);
                return;
            }
            target.add_component<mesh_component>(mesh_component{material, primitive.mesh});
        }

        void spawn(spawn_context& ctx, std::size_t index, node& parent, bool is_root)
        {
            const rendering_engine::gltf_model& model = ctx.model;
            if (index >= model.nodes.size())
            {
                LOG_WRN("gltf: node index %zu is out of range; skipped", index);
                return;
            }
            if (ctx.visited[index])
            {
                LOG_WRN("gltf: node '%s' is reachable twice (a cycle or a shared child); skipped",
                        model.nodes[index].name.c_str());
                return;
            }
            ctx.visited[index] = true;

            const rendering_engine::gltf_node& source = model.nodes[index];
            // Created linked under the parent, so the node carries the
            // scene's component store before any mesh component is attached.
            node& current = ctx.scene.create_node(source.name, &parent);

            math::vec3 position = source.translation;
            math::quat rotation = source.rotation;
            if (is_root)
            {
                position = k_gltf_to_engine * position;
                rotation = k_gltf_to_engine * rotation;
                ctx.roots.push_back(&current);
            }
            current.transform.set_position(position);
            current.transform.set_quaternion(rotation);
            current.transform.set_scale(source.scale);
            ctx.spawned[index] = &current;
            ctx.is_root[index] = is_root;

            if (source.primitives.size() == 1)
            {
                attach_primitive(ctx, index, source.primitives[0], current);
            }
            else
            {
                // A node holds one component per type, so several primitives
                // fan out into one child each.
                for (std::size_t k = 0; k < source.primitives.size(); ++k)
                {
                    node& child = ctx.scene.create_node(source.name + "/primitive" + std::to_string(k), &current);
                    attach_primitive(ctx, index, source.primitives[k], child);
                }
            }

            for (const std::size_t child : source.children)
            {
                spawn(ctx, child, current, false);
            }
        }

        // Puts an animator on the first spawned root when the model has clips
        // or skinned meshes: every spawned node follows its joint (the roots
        // through the up-axis turn), every skinned mesh takes its skin's
        // palette, and the first clip starts looping.
        void attach_animator(spawn_context& ctx)
        {
            const rendering_engine::gltf_model& model = ctx.model;
            const bool has_skinned_mesh = std::any_of(ctx.skinned_meshes.begin(),
                                                      ctx.skinned_meshes.end(),
                                                      [](const std::vector<node*>& meshes) { return !meshes.empty(); });
            if (model.node_skeleton == nullptr || ctx.roots.empty() || (model.animations.empty() && !has_skinned_mesh))
            {
                return;
            }

            animator_component animator{model.node_skeleton, model.animations};
            for (std::size_t k = 0; k < ctx.spawned.size(); ++k)
            {
                if (ctx.spawned[k] != nullptr)
                {
                    animator.bind_node(k, *ctx.spawned[k], ctx.is_root[k] ? k_gltf_to_engine : math::quat{});
                }
                for (node* mesh : ctx.skinned_meshes[k])
                {
                    animator.bind_skin(model.nodes[k].skin, k, *mesh);
                }
            }
            if (!model.animations.empty())
            {
                animator.play(std::size_t{0});
            }
            ctx.roots.front()->add_component<animator_component>(std::move(animator));
        }
    } // namespace

    std::vector<node*> instantiate_gltf(const rendering_engine::gltf_model& model, node& parent)
    {
        std::vector<node*> roots;
        runtime::scene* scene = parent.scene();
        if (scene == nullptr)
        {
            LOG_ERR("gltf: the parent node belongs to no scene; nothing instantiated");
            return roots;
        }
        if (scene->is_traversing())
        {
            LOG_ERR("gltf: the parent's scene is mid-traversal; instantiate from outside its update");
            return roots;
        }

        spawn_context ctx{model, *scene, roots};
        for (const std::size_t root : model.root_nodes)
        {
            spawn(ctx, root, parent, true);
        }
        attach_animator(ctx);
        return roots;
    }

    core::subscription instantiate_gltf_when_ready(std::shared_ptr<const rendering_engine::gltf_asset> asset,
                                                   node& parent,
                                                   std::function<void(const std::vector<node*>& roots)> on_spawned)
    {
        if (asset == nullptr)
        {
            return {};
        }
        node* target = &parent;
        bool handled = false;
        return current_engine().events->subscribe<core::render_update>(
            [asset = std::move(asset), target, on_spawned = std::move(on_spawned), handled](
                const core::render_update&) mutable
            {
                if (handled || asset->state == rendering_engine::gltf_asset::load_state::loading)
                {
                    return;
                }
                handled = true;
                if (!asset->is_ready())
                {
                    LOG_WRN("gltf: the model did not load (%s); nothing instantiated", asset->error.c_str());
                    return;
                }
                const std::vector<node*> roots = instantiate_gltf(asset->model, *target);
                if (on_spawned)
                {
                    on_spawned(roots);
                }
            });
    }
} // namespace runtime
