/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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

#include <scene_graph/scene_manager.hpp>

#include <infrastructure/log.hpp>

#include <stdexcept>
#include <utility>

void scene_graph::scene_manager::push(std::unique_ptr<scene> next)
{
    if (!next)
    {
        LOG_ERR("scene_manager::push called with null scene; ignoring");
        return;
    }

    m_stack.push_back(std::move(next));
    LOG_INF("scene_manager: pushed scene (depth now=%zu)", m_stack.size());
    m_stack.back()->on_load();
}

void scene_graph::scene_manager::replace(std::unique_ptr<scene> next)
{
    if (!next)
    {
        LOG_ERR("scene_manager::replace called with null scene; ignoring");
        return;
    }

    if (!m_stack.empty())
    {
        m_stack.back()->on_unload();
        m_stack.pop_back();
        LOG_INF("scene_manager: replace unloaded previous top (depth now=%zu)", m_stack.size());
    }

    m_stack.push_back(std::move(next));
    LOG_INF("scene_manager: replace pushed new top (depth now=%zu)", m_stack.size());
    m_stack.back()->on_load();
}

void scene_graph::scene_manager::pop()
{
    if (m_stack.empty())
    {
        LOG_WRN("scene_manager::pop on empty stack; ignoring");
        return;
    }

    m_stack.back()->on_unload();
    m_stack.pop_back();
    LOG_INF("scene_manager: popped scene (depth now=%zu)", m_stack.size());
}

scene_graph::scene& scene_graph::scene_manager::active()
{
    if (m_stack.empty())
    {
        throw std::runtime_error{"scene_manager::active called on empty stack"};
    }
    return *m_stack.back();
}

bool scene_graph::scene_manager::has_active() const
{
    return !m_stack.empty();
}

std::size_t scene_graph::scene_manager::size() const
{
    return m_stack.size();
}
