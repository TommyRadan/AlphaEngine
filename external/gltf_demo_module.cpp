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
 * @file gltf_demo_module.cpp
 * @brief Showcase for the glTF importer: loads the .gltf / .glb named by the
 *        ALPHAENGINE_GLTF environment variable, spawns it into a scene of its
 *        own and frames it with the camera.
 *
 * Set ALPHAENGINE_GLTF to a model path before launching. The model is loaded
 * through rendering_engine::load_gltf (meshes and textures through the asset
 * cache, one standard_material per glTF material) and instantiated with
 * runtime::instantiate_gltf, which rotates the +Y-up file into the engine's
 * +Z-up world. A directional key light plus a dim ambient fill light it. The
 * @c gltf_showcase behaviour on the demo's root node owns the model and, on
 * the first frame a camera is rendering, puts that camera in front of the
 * model's bounds looking at its centre; WASD / mouse-look take over from there.
 */

#include "api/game_module.hpp"

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/gltf_importer.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/engine.hpp>
#include <runtime/gltf_instantiate.hpp>
#include <runtime/scene_manager.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <utility>

namespace
{
    namespace math = core::math;

    // The root rotation instantiate_gltf applies (+90 degrees about X), so the
    // framing box is computed in the same space the nodes end up in.
    constexpr float k_half_sqrt2 = 0.70710678118f;
    constexpr math::quat k_gltf_to_engine{k_half_sqrt2, k_half_sqrt2, 0.0f, 0.0f};

    math::mat4 local_matrix(const rendering_engine::gltf_node& node)
    {
        return math::translate(node.translation) * math::to_mat4(node.rotation) * math::scale(node.scale);
    }

    // The node's matrix in the file's own (+Y up) space, composed up the
    // parent chain.
    math::mat4 file_space_matrix(const rendering_engine::gltf_model& model, std::size_t index)
    {
        math::mat4 result = local_matrix(model.nodes[index]);
        std::size_t parent = model.nodes[index].parent;
        while (parent != rendering_engine::gltf_npos)
        {
            result = local_matrix(model.nodes[parent]) * result;
            parent = model.nodes[parent].parent;
        }
        return result;
    }

    // World-space box around every imported primitive, for framing: each
    // primitive's bounds boxed through its node's file-space matrix and the
    // up-axis conversion, corner by corner. False when the model has no
    // primitive with bounds.
    bool compute_bounds(const rendering_engine::gltf_model& model, math::aabb& bounds)
    {
        bool has_bounds = false;
        for (std::size_t n = 0; n < model.nodes.size(); ++n)
        {
            if (model.nodes[n].primitives.empty())
            {
                continue;
            }
            const math::mat4 world = file_space_matrix(model, n);
            for (const std::size_t p : model.nodes[n].primitives)
            {
                if (model.primitives[p].mesh == nullptr)
                {
                    continue;
                }
                const math::aabb& box = model.primitives[p].mesh->bounds;
                for (int corner = 0; corner < 8; ++corner)
                {
                    const math::vec3 local{(corner & 1) != 0 ? box.max.x : box.min.x,
                                           (corner & 2) != 0 ? box.max.y : box.min.y,
                                           (corner & 4) != 0 ? box.max.z : box.min.z};
                    const math::vec4 transformed = world * math::vec4{local, 1.0f};
                    const math::vec3 point = k_gltf_to_engine * math::vec3{transformed.x, transformed.y, transformed.z};
                    bounds = has_bounds ? math::merge(bounds, point) : math::aabb{point, point};
                    has_bounds = true;
                }
            }
        }
        return has_bounds;
    }

    // The node in @p scene whose camera_component carries @p camera, or null.
    runtime::node* find_camera_node(runtime::context& scene, const rendering_engine::camera* camera)
    {
        runtime::node* found = nullptr;
        scene.each<runtime::camera_component>(
            [&found, camera](runtime::node& holder, runtime::camera_component& component)
            {
                if (component.get() == camera)
                {
                    found = &holder;
                }
            });
        return found;
    }

    // The demo's root: owns the loaded model, whose materials the spawned
    // nodes (its descendants, freed first) draw with, and frames the model
    // with the rendering camera once there is one.
    struct gltf_showcase final : runtime::behavior
    {
        explicit gltf_showcase(std::unique_ptr<rendering_engine::gltf_model> model) : m_model{std::move(model)}
        {
            m_has_bounds = compute_bounds(*m_model, m_bounds);
        }

        const rendering_engine::gltf_model& model() const noexcept
        {
            return *m_model;
        }

        void on_update(float delta_time) override
        {
            (void)delta_time;
            if (m_camera_placed || !m_has_bounds)
            {
                return;
            }
            // The camera comes from another module (camera_module); until
            // one is rendering there is nothing to place.
            rendering_engine::camera* camera = rendering_engine::active_camera();
            if (camera == nullptr)
            {
                return;
            }

            const math::vec3 center = m_bounds.center();
            const float radius = std::max(math::length(m_bounds.extents()), 0.05f);
            // Back off along -Y (in front of the converted model) and a little
            // up, far enough that the whole box fits a ~60 degree view.
            const float distance = radius * 2.2f;
            const math::vec3 eye = center + math::vec3{0.0f, -distance, distance * 0.35f};

            // A camera on a node views from the node's pose (a fly camera
            // reads it back every frame), so place the node; a bare camera is
            // placed directly.
            runtime::node* rig = find_camera_node(runtime::current_engine().scenes->persistent_scene(), camera);
            if (rig == nullptr && owner().scene() != nullptr)
            {
                rig = find_camera_node(*owner().scene(), camera);
            }
            if (rig != nullptr)
            {
                rig->set_world_position(eye);
                rig->look_at(center);
            }
            else
            {
                camera->transform.set_position(eye);
                camera->look_at(center);
            }
            m_camera_placed = true;
        }

    private:
        std::unique_ptr<rendering_engine::gltf_model> m_model;
        math::aabb m_bounds{};
        bool m_has_bounds{false};
        bool m_camera_placed{false};
    };
} // namespace

GAME_MODULE()
{
    const char* path = std::getenv("ALPHAENGINE_GLTF");
    if (path == nullptr || *path == '\0')
    {
        LOG_WRN("gltf_demo_module: set ALPHAENGINE_GLTF to a .gltf / .glb path to load a model");
        return;
    }

    std::unique_ptr<rendering_engine::gltf_model> model;
    try
    {
        model = std::make_unique<rendering_engine::gltf_model>(rendering_engine::load_gltf(path));
    }
    catch (const std::exception& error)
    {
        LOG_ERR("gltf_demo_module: could not load '%s': %s", path, error.what());
        return;
    }

    // The model goes into a scene of its own, which the engine unloads on
    // shutdown: the spawned nodes first, then the showcase holding the model
    // whose materials they drew with, all before the renderer tears down.
    runtime::context& demo = runtime::current_engine().scenes->load("gltf_demo", runtime::load_mode::additive);
    runtime::node& root = demo.create_node("gltf_demo");
    gltf_showcase* showcase = runtime::add_behavior<gltf_showcase>(root, std::move(model));
    if (showcase == nullptr)
    {
        return;
    }
    runtime::instantiate_gltf(showcase->model(), root);

    auto ambient = std::make_unique<rendering_engine::ambient_light>();
    ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
    ambient->intensity = 0.25f;
    root.add_component(runtime::light_component{std::move(ambient)});

    // Key light from above and in front (the file's front faces -Y once
    // converted), tilted so surfaces facing the camera are lit. The light
    // component keeps its direction on the node's forward (+X) axis.
    auto sun_light = std::make_unique<rendering_engine::directional_light>();
    sun_light->color = math::vec3{1.0f, 0.97f, 0.92f};
    sun_light->intensity = 2.0f;
    sun_light->cast_shadow = true;
    runtime::node& sun = demo.create_node("sun", &root);
    sun.look_at(sun.world_position() + math::normalize(math::vec3{0.3f, 0.6f, -0.75f}));
    sun.add_component(runtime::light_component{std::move(sun_light)});
}
