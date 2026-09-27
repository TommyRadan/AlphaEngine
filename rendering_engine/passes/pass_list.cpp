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

#include <rendering_engine/passes/pass_list.hpp>

#include <unordered_set>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>

namespace rendering_engine
{
    void pass_list::import_external(std::string_view resource)
    {
        m_external.emplace_back(resource);
    }

    bool pass_list::validate() const
    {
        // A resource is "produced" once an imported external declares it or an
        // earlier pass writes it. Walking in list order, a read of an
        // unproduced resource means the list is mis-ordered (or a pass forgot
        // to declare a write) — exactly the class of bug a hand-ordered list
        // can hide.
        std::unordered_set<std::string> produced(m_external.begin(), m_external.end());
        bool hazard_free = true;
        for (const auto& p : m_passes)
        {
            pass_io_builder io;
            p->declare_io(io);
            for (const auto& r : io.reads())
            {
                if (produced.find(r) == produced.end())
                {
                    LOG_ERR(
                        "pass_list: pass '%s' reads resource '%s' before any pass produces it", p->name(), r.c_str());
                    hazard_free = false;
                }
            }
            for (const auto& w : io.writes())
            {
                produced.insert(w);
            }
        }
        LOG_INF("pass_list: validated %zu passes (%s)",
                m_passes.size(),
                hazard_free ? "no hazards" : "hazards found — see errors");
        return hazard_free;
    }

    void pass_list::prepare(const frame_context& ctx) const
    {
        for (const auto& p : m_passes)
        {
            p->prepare(ctx);
        }
    }

    void pass_list::record(gpu::command_encoder& encoder, const frame_context& ctx, pass_hooks* hooks) const
    {
        for (size_t i = 0; i < m_passes.size(); ++i)
        {
            pass& p = *m_passes[i];
            const char* name = p.name();
            // The debug group names the pass in a graphics debugger's
            // command tree; the hooks (the GPU profiler) bracket it.
            encoder.push_debug_group(name);
            if (hooks != nullptr)
            {
                hooks->before_pass(encoder, i, name);
            }
            p.record(encoder, ctx);
            if (hooks != nullptr)
            {
                hooks->after_pass(encoder, i, name);
            }
            encoder.pop_debug_group();
        }
    }

    void pass_list::resize(uint32_t width, uint32_t height) const
    {
        for (const auto& p : m_passes)
        {
            p->resize(width, height);
        }
    }

    std::vector<std::string> pass_list::pass_names() const
    {
        std::vector<std::string> names;
        names.reserve(m_passes.size());
        for (const auto& p : m_passes)
        {
            names.emplace_back(p->name());
        }
        return names;
    }

    void pass_list::clear()
    {
        m_passes.clear();
        m_external.clear();
    }
} // namespace rendering_engine
