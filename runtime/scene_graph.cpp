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
#include <utility>

#include <core/jobs.hpp>
#include <core/log.hpp>
#include <runtime/engine.hpp>

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
    // Phase 1 (parallel): warm every active node's cached world matrix,
    // breadth-first by depth. Within one depth band every node's parent was
    // resolved in the previous band, so a node only ever writes its own cache
    // while reading its parent's already-settled one — the writes are disjoint
    // and the reads are pure, which makes the band data-race free. This keeps
    // the matrix multiplies (the costly part of a deep hierarchy) on the worker
    // pool while everything with side effects below stays on the main thread.
    //
    // Inactive subtrees are skipped, matching node::update_subtree: a disabled
    // node freezes its whole subtree and its components are already hidden.
    core::jobs& jobs = *current_engine().jobs;

    m_band.clear();
    if (root.is_active())
    {
        m_band.push_back(&root);
    }

    while (!m_band.empty())
    {
        jobs.parallel_for(m_band.size(), [this](std::size_t i) { m_band[i]->transform.get_world_matrix(); });

        m_next_band.clear();
        for (node* parent : m_band)
        {
            for (node* child : parent->children())
            {
                if (child->is_active())
                {
                    m_next_band.push_back(child);
                }
            }
        }
        std::swap(m_band, m_next_band);
    }

    // Phase 2 (serial): dispatch each component's on_update down the tree.
    // Components bridge to shared subsystems (renderer registries, the light
    // list, per-draw GPU buffers), none of which are thread-safe, so this walk
    // stays single-threaded — and the world matrices it reads are already warm.
    root.update_subtree();
}
