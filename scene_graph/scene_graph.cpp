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

#include <scene_graph/scene_graph.hpp>

#include <event_engine/event.hpp>
#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>

#include <string>

void scene_graph::context::init()
{
    LOG_INF("Init Scene Graph");

    // Dispatch render_scene events through the ECS world so that entities
    // carrying a renderable_component can draw without each game module
    // having to subscribe to render_scene directly. See docs/logging.md.
    event_engine::context::get_instance().register_listener(event_engine::event_type::render_scene,
                                                            [](const event_engine::event&)
                                                            {
                                                                auto& world = context::get_instance().get_world();
                                                                for (auto id : world.view<renderable_component>())
                                                                {
                                                                    auto* renderable =
                                                                        world.get<renderable_component>(id);
                                                                    if (renderable != nullptr && renderable->draw)
                                                                    {
                                                                        renderable->draw();
                                                                    }
                                                                }
                                                            });
}

void scene_graph::context::quit()
{
    LOG_INF("Quit Scene Graph: %zu entities still alive", m_world.entity_count());
    m_world.clear();
}
