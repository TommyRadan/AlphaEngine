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
 *        ALPHAENGINE_GLTF environment variable, spawns it under the scene
 *        root and frames it with the camera.
 *
 * Set ALPHAENGINE_GLTF to a model path before launching. The model is loaded
 * through rendering_engine::load_gltf (meshes and textures through the asset
 * cache, one standard_material per glTF material) and instantiated with
 * runtime::instantiate_gltf, which rotates the +Y-up file into the engine's
 * +Z-up world. A directional key light plus a dim ambient fill light it; the
 * camera (owned by camera_module, so it is placed on the first frame once it
 * exists) is put in front of the model's bounds looking at its centre, and
 * WASD / mouse-look take over from there.
 */

#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/gltf_importer.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <runtime/engine.hpp>
#include <runtime/gltf_instantiate.hpp>
#include <runtime/node.hpp>
#include <runtime/scene_graph.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

namespace
{
    namespace math = core::math;

    // Owned here: the nodes must go before the model (their mesh components
    // draw with its materials) and both before the renderer.
    std::unique_ptr<rendering_engine::gltf_model> g_model;
    std::vector<std::unique_ptr<runtime::node>> g_nodes;
    std::unique_ptr<rendering_engine::ambient_light> g_ambient;
    std::unique_ptr<rendering_engine::directional_light> g_sun;

    // World-space box around every imported primitive, for framing.
    math::aabb g_bounds{};
    bool g_has_bounds = false;
    bool g_camera_placed = false;

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

    void merge_bounds(const math::vec3& point)
    {
        g_bounds = g_has_bounds ? math::merge(g_bounds, point) : math::aabb{point, point};
        g_has_bounds = true;
    }

    // Box every primitive's bounds through its node's file-space matrix and
    // the up-axis conversion, corner by corner.
    void compute_bounds(const rendering_engine::gltf_model& model)
    {
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
                    merge_bounds(k_gltf_to_engine * math::vec3{transformed.x, transformed.y, transformed.z});
                }
            }
        }
    }

    void on_engine_start(const core::engine_start& event)
    {
        (void)event;

        const char* path = std::getenv("ALPHAENGINE_GLTF");
        if (path == nullptr || *path == '\0')
        {
            LOG_WRN("gltf_demo_module: set ALPHAENGINE_GLTF to a .gltf / .glb path to load a model");
            return;
        }

        try
        {
            g_model = std::make_unique<rendering_engine::gltf_model>(rendering_engine::load_gltf(path));
        }
        catch (const std::exception& error)
        {
            LOG_ERR("gltf_demo_module: could not load '%s': %s", path, error.what());
            return;
        }

        g_nodes = runtime::instantiate_gltf(*g_model, runtime::current_engine().scenes->root);
        compute_bounds(*g_model);

        g_ambient = std::make_unique<rendering_engine::ambient_light>();
        g_ambient->color = math::vec3{1.0f, 1.0f, 1.0f};
        g_ambient->intensity = 0.25f;

        // Key light from above and in front (the file's front faces -Y once
        // converted), tilted so surfaces facing the camera are lit.
        g_sun = std::make_unique<rendering_engine::directional_light>();
        g_sun->color = math::vec3{1.0f, 0.97f, 0.92f};
        g_sun->intensity = 2.0f;
        g_sun->direction = math::normalize(math::vec3{0.3f, 0.6f, -0.75f});
        g_sun->cast_shadow = true;
    }

    void on_engine_stop(const core::engine_stop& event)
    {
        (void)event;
        g_sun.reset();
        g_ambient.reset();
        // Nodes first (unregistering every mesh component), then the model
        // whose materials they drew with, before the renderer tears down.
        g_nodes.clear();
        g_model.reset();
    }

    void on_render_update(const core::render_update& event)
    {
        (void)event;
        if (g_camera_placed || !g_has_bounds)
        {
            return;
        }
        // camera_module creates and attaches the camera in its own
        // on_engine_start, which may run after ours; wait for it.
        rendering_engine::camera* camera = rendering_engine::camera::get_current_camera();
        if (camera == nullptr)
        {
            return;
        }

        const math::vec3 center = g_bounds.center();
        const float radius = std::max(math::length(g_bounds.extents()), 0.05f);
        // Back off along -Y (in front of the converted model) and a little
        // up, far enough that the whole box fits a ~60 degree view.
        const float distance = radius * 2.2f;
        camera->transform.set_position(center + math::vec3{0.0f, -distance, distance * 0.35f});
        camera->look_at(center);
        g_camera_placed = true;
    }
} // namespace

GAME_MODULE()
{
    LOG_INF("Registering external module: gltf_demo_module");
    struct game_module_info info = {};
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_render_update = on_render_update;
    register_game_module(info);
    return true;
}
