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

#include <rendering_engine/render_graph/frame_graph.hpp>

#include <algorithm>
#include <unordered_set>
#include <utility>

#include <core/log.hpp>

namespace rendering_engine::render_graph
{
    void pass_io_builder::read(std::string_view resource)
    {
        m_reads.emplace_back(resource);
    }

    void pass_io_builder::write(std::string_view resource)
    {
        m_writes.emplace_back(resource);
    }

    void frame_graph::import_external(std::string_view resource)
    {
        m_external.emplace_back(resource);
    }

    void frame_graph::add_pass(std::string name, pass_io_builder io, execute_fn execute)
    {
        m_nodes.push_back({std::move(name), std::move(io), std::move(execute)});
    }

    bool frame_graph::compile()
    {
        // A resource is "produced" once an imported external declares it or an
        // earlier pass writes it. Walking in registration order, a read of an
        // unproduced resource means the pass list is mis-ordered (or a pass
        // forgot to declare a write) — exactly the class of bug the legacy
        // hand-ordered loop could hide.
        std::unordered_set<std::string> produced(m_external.begin(), m_external.end());
        bool hazard_free = true;
        for (const auto& n : m_nodes)
        {
            for (const auto& r : n.io.reads())
            {
                if (produced.find(r) == produced.end())
                {
                    LOG_WRN("frame_graph: pass '%s' reads resource '%s' before any pass produces it",
                            n.name.c_str(),
                            r.c_str());
                    hazard_free = false;
                }
            }
            for (const auto& w : n.io.writes())
            {
                produced.insert(w);
            }
        }
        LOG_INF("frame_graph: compiled %zu passes (%s)",
                m_nodes.size(),
                hazard_free ? "no hazards" : "hazards found — see warnings");
        return hazard_free;
    }

    void frame_graph::execute(gpu::command_encoder& encoder, const frame_context& ctx) const
    {
        for (const auto& n : m_nodes)
        {
            if (n.execute)
            {
                n.execute(encoder, ctx);
            }
        }
    }

    void frame_graph::clear()
    {
        m_nodes.clear();
        m_external.clear();
    }
} // namespace rendering_engine::render_graph
