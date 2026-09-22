/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <runtime/scene_graph.hpp>

#include <string>

#include <core/log.hpp>

runtime::context::context()
{
    // Wire the root to the scene's component store so every node added under it
    // inherits the store (via node::add) and can carry components.
    root.set_store(&components);
}

void runtime::context::init()
{
    LOG_INF("Init Scene Graph");
    // The root node is always present; subtrees parent under it via node::add.
    // Node add/remove and traversal diagnostics are expected to be logged from
    // scene_graph API calls. See docs/logging.md.
}

void runtime::context::quit()
{
    LOG_INF("Quit Scene Graph");
}

void runtime::context::update()
{
    // One serial depth-first walk. Each component's on_update runs against
    // world matrices that transform::get_world_matrix resolves lazily (and
    // caches) as the walk reaches them, parents before children.
    //
    // An earlier version warmed those caches first with a parallel_for over
    // each depth band of the tree. It was removed without a replacement: the
    // per-node work is a version compare and one 4x4 multiply, far cheaper
    // than the std::function allocation, queue push and worker wake it cost
    // to farm out, and no measurement ever showed the scene sizes this engine
    // draws gaining from it. Its race-freedom also rested on invariants
    // nothing enforces — that every transform's parent is the transform of a
    // node one band up — while transform::set_parent is public and
    // mesh_component already parents a non-node transform. Bring parallelism
    // back here only against a measured workload, with those ownership rules
    // made explicit first.
    //
    // Components bridge to shared subsystems (renderer registries, the light
    // list, per-draw GPU buffers), none of which are thread-safe, so the walk
    // itself must stay on the main thread regardless.
    root.update_subtree();
}
